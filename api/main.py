import datetime, os, uvicorn, locale, ics, requests
from fastapi import FastAPI, HTTPException, Depends, Request, Response
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel
from typing import Annotated, Tuple, List
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from google.oauth2 import service_account
from dotenv import load_dotenv

load_dotenv()

app = FastAPI(swagger_ui_parameters={"persistAuthorization": True})

SCOPES = [
    "https://www.googleapis.com/auth/spreadsheets",
    "https://www.googleapis.com/auth/calendar",
]
SPREADSHEET_ID = os.environ["SPREADSHEET_ID"]
CALENDAR_ID = os.environ["CALENDAR_ID"]
SERVICE_ACCOUNT_FILE = os.environ.get("SERVICE_ACCOUNT_FILE", "secrets/service.json")

# Authorization token used for the display
AUTHORIZATION_TOKEN = os.environ["AUTHORIZATION_TOKEN"]

TOBIS_KOCHBUCH_URL = os.environ.get("TOBIS_KOCHBUCH_URL", None)

SHOPPING_LIST_URL = os.environ.get("SHOPPING_LIST_URL", None)
SHOPPING_LIST_TOKEN = os.environ.get("SHOPPING_LIST_TOKEN", None)
SHOPPING_LIST_GROUP = os.environ.get("SHOPPING_LIST_GROUP", None)

KEYCLOAK_BASE_URL = os.environ.get("KEYCLOAK_BASE_URL", None)
KEYCLOAK_REALM = os.environ.get("KEYCLOAK_REALM", None)
KEYCLOAK_CLIENT_ID = os.environ.get("KEYCLOAK_CLIENT_ID", None)
KEYCLOAK_CLIENT_SECRET = os.environ.get("KEYCLOAK_CLIENT_SECRET", None)
KEYCLOAK_GROUP_ID = os.environ.get("KEYCLOAK_GROUP_ID", None)


class TokenAuthenticationScheme(HTTPBearer):
    def __init__(self, auto_error: bool = True):
        super(TokenAuthenticationScheme, self).__init__(auto_error=auto_error)

    async def __call__(self, request: Request):
        credentials: HTTPAuthorizationCredentials = await super(
            TokenAuthenticationScheme, self
        ).__call__(request)
        if not credentials:
            raise HTTPException(status_code=403, detail="Invalid authorization code.")

        if not credentials.scheme == "Bearer":
            raise HTTPException(
                status_code=403, detail="Invalid authentication scheme."
            )

        if not credentials.credentials == AUTHORIZATION_TOKEN:
            raise HTTPException(status_code=403, detail="Invalid token.")

        return credentials.credentials


tokenScheme = TokenAuthenticationScheme()
googleCredentials = service_account.Credentials.from_service_account_file(
    SERVICE_ACCOUNT_FILE, scopes=SCOPES
)
googleSheets = build("sheets", "v4", credentials=googleCredentials).spreadsheets()
googleCalendar = build("calendar", "v3", credentials=googleCredentials).events()

locale.setlocale(locale.LC_TIME, "de_DE.UTF-8")


@app.get("/")
def read_root():
    return "WG-API v1.0.0"


@app.get("/mealcount")
def read_mealcount(
    _: Annotated[str, Depends(tokenScheme)],
) -> Tuple[int, int, int, int]:
    result = (
        googleSheets.values().get(spreadsheetId=SPREADSHEET_ID, range="C3:F3").execute()
    )
    values = result.get("values", [])
    return (int(values[0][0]), int(values[0][1]), int(values[0][2]), int(values[0][3]))


def get_mealcount_names_google_sheets() -> Tuple[str, str, str, str]:
    result = (
        googleSheets.values().get(spreadsheetId=SPREADSHEET_ID, range="C2:F2").execute()
    )
    values = result.get("values", [])
    return (values[0][0], values[0][1], values[0][2], values[0][3])


def get_mealcount_names() -> Tuple[str, str, str, str]:
    members_tuple = get_group_members_in_tuple()
    return (
        members_tuple[0][1],
        members_tuple[1][1],
        members_tuple[2][1],
        members_tuple[3][1],
    )


def get_group_members_in_tuple() -> (
    Tuple[Tuple[str, str], Tuple[str, str], Tuple[str, str], Tuple[str, str]]
):
    members_tuple = [("", ""), ("", ""), ("", ""), ("", "")]
    members = get_group_members_from_keycloak()
    for member in members:
        username, first_name, display_index = member
        if display_index < 0 or display_index > 3:
            continue
        members_tuple[display_index] = (username, first_name)

    return tuple(members_tuple)


def get_group_members_from_keycloak() -> List[Tuple[str, str, int]]:
    # Get a short-lived access token using the static client secret
    token_response = requests.post(
        f"{KEYCLOAK_BASE_URL}/realms/{KEYCLOAK_REALM}/protocol/openid-connect/token",
        data={
            "grant_type": "client_credentials",
            "client_id": KEYCLOAK_CLIENT_ID,
            "client_secret": KEYCLOAK_CLIENT_SECRET,
        },
        timeout=10,
    )
    token_response.raise_for_status()

    access_token = token_response.json()["access_token"]

    # Fetch members of the permitted group
    response = requests.get(
        f"{KEYCLOAK_BASE_URL}/admin/realms/{KEYCLOAK_REALM}/groups/{KEYCLOAK_GROUP_ID}/members",
        headers={
            "Authorization": f"Bearer {access_token}",
        },
        timeout=10,
    )
    response.raise_for_status()

    members = []
    for member in response.json():
        if (
            not member.get("attributes")
            or not member["attributes"].get("wg-display-index")
            or len(member["attributes"]["wg-display-index"]) == 0
        ):
            continue

        members.append(
            (
                member["username"],
                member["firstName"],
                int(member["attributes"]["wg-display-index"][0]),
            )
        )

    return members


@app.get("/mealcount/names")
def read_mealcount_names(
    _: Annotated[str, Depends(tokenScheme)],
) -> Tuple[str, str, str, str]:
    try:
        return get_mealcount_names()
    except HttpError as e:
        raise HTTPException(status_code=400, detail="Error")


@app.post("/mealcount")
def update_mealcount(
    to_insert: Tuple[int, int, int, int], _: Annotated[str, Depends(tokenScheme)]
):
    print("Request: ", to_insert)

    if len(to_insert) != 4 or any(not isinstance(x, int) for x in to_insert):
        raise HTTPException(status_code=400, detail="Invalid request")

    google_sheets_names = get_mealcount_names_google_sheets()
    keycloak_group_members = get_group_members_in_tuple()

    for keycloak_member, google_sheets_name in zip(
        keycloak_group_members, google_sheets_names
    ):
        if keycloak_member[1] != google_sheets_name:
            raise HTTPException(
                status_code=500,
                detail=f"Keycloak group member {keycloak_member[1]} does not match Google Sheets name {google_sheets_name}",
            )

    push_mealcount_to_google_sheets(to_insert)
    push_mealcount_to_shopping_list(to_insert, keycloak_group_members)


def push_mealcount_to_google_sheets(to_insert: Tuple[int, int, int, int]):
    try:
        googleSheets.values().update(
            spreadsheetId=SPREADSHEET_ID,
            range="C3:F3",
            valueInputOption="USER_ENTERED",
            body={"values": [list(to_insert)]},
        ).execute()
    except HttpError as e:
        raise HTTPException(status_code=500, detail="Internal server Error")


def push_mealcount_to_shopping_list(
    counts: Tuple[int, int, int, int],
    keycloak_group_members: Tuple[
        Tuple[str, str], Tuple[str, str], Tuple[str, str], Tuple[str, str]
    ],
):
    if SHOPPING_LIST_URL is None or SHOPPING_LIST_TOKEN is None:
        return

    body = {
        "counts": [
            {"user": member[0], "count": count}
            for member, count in zip(keycloak_group_members, counts)
        ]
    }
    if SHOPPING_LIST_GROUP is not None:
        body["group"] = SHOPPING_LIST_GROUP

    try:
        response = requests.post(
            f"{SHOPPING_LIST_URL}/api/meals",
            json=body,
            headers={"Authorization": f"Bearer {SHOPPING_LIST_TOKEN}"},
            timeout=5,
        )
    except requests.RequestException as e:
        print("Failed to reach the shopping list: ", e)
        return

    if response.status_code != 200:
        print("Shopping list rejected the mealcount: ", response.status_code)


@app.get("/events/reminder.ics")
def events_ical(_: Annotated[str, Depends(tokenScheme)]) -> str:
    morning = datetime.datetime.now().astimezone() - datetime.timedelta(days=3)
    evening = datetime.datetime.now().astimezone() + datetime.timedelta(days=3)

    events = googleCalendar.list(
        calendarId=CALENDAR_ID,
        singleEvents=True,
        timeMin=morning.isoformat(),
        timeMax=evening.isoformat(),
    ).execute()

    ical = ics.icalendar.Calendar()
    for event in events["items"]:
        if "date" not in event["start"] or "date" not in event["end"]:
            continue

        start = datetime.datetime.strptime(event["start"]["date"], "%Y-%m-%d")
        end = datetime.datetime.strptime(
            event["end"]["date"], "%Y-%m-%d"
        ) - datetime.timedelta(days=2)

        print(
            "{}({}) - {}({}): {}".format(
                start,
                event["start"]["date"],
                end,
                event["end"]["date"],
                event["summary"],
            )
        )

        alarmTime = datetime.timedelta(days=-1)
        alarm = ics.alarm.DisplayAlarm(trigger=alarmTime)

        ical_event = ics.icalendar.Event(
            uid=event["id"],
            name=event["summary"],
            begin=start,
            end=end,
            alarms=[alarm],
        )
        ical_event.make_all_day()

        ical.events.add(ical_event)

    return Response(ical.serialize())


class EventsResponse(BaseModel):
    events: List[str]
    mealcountNames: Tuple[str, str, str, str]
    mealPlannedToday: str | None
    secondsUntilMidnight: int
    day: int
    month: int
    dayString: str


def get_event_names() -> List[str]:
    morning = datetime.datetime.now().astimezone().replace(hour=12, minute=0, second=0)
    evening = datetime.datetime.now().astimezone().replace(hour=13, minute=0, second=0)

    events = googleCalendar.list(
        calendarId=CALENDAR_ID,
        singleEvents=True,
        timeMin=morning.isoformat(),
        timeMax=evening.isoformat(),
    ).execute()

    eventNames = []

    for event in events["items"]:
        # print("{}-{}: {}".format(event["start"]["date"], event["end"]["date"] , event["summary"]))
        eventName = event["summary"]

        if ":" in eventName:
            parts = eventName.split(":")
            eventName = f"{parts[1].strip()}: {parts[0].strip()}"

        eventNames.append(eventName)

    return eventNames


def get_meal_planned_today() -> str | None:
    if TOBIS_KOCHBUCH_URL is None:
        return None

    plannedTodayResponse = requests.get(
        f"{TOBIS_KOCHBUCH_URL}/recipe/planned-today",
        headers={"x-forwarded-roles": "group:WG-Gang2"},
        timeout=5,
    )
    if plannedTodayResponse.status_code != 200:
        return None

    return plannedTodayResponse.json().get("title", None)


@app.get("/motd")
def message_of_the_day(_: Annotated[str, Depends(tokenScheme)]) -> EventsResponse:
    eventNames = get_event_names()
    mealcountNames = get_mealcount_names()
    mealPlannedToday = get_meal_planned_today()

    now = datetime.datetime.now().astimezone()
    midnight = (
        datetime.datetime.now().astimezone().replace(hour=23, minute=59, second=59)
    )
    secondsUntilMidnight = (midnight - now).seconds

    return {
        "events": eventNames,
        "mealcountNames": mealcountNames,
        "mealPlannedToday": mealPlannedToday,
        "day": now.day,
        "month": now.month,
        "dayString": now.strftime("%A"),
        "secondsUntilMidnight": secondsUntilMidnight,
    }


def main():
    pass


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000)
