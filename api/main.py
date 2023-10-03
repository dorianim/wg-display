import datetime, os, uvicorn, locale
from fastapi import FastAPI, HTTPException, Depends, Request
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel
from typing import Annotated, Tuple, List
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from google.oauth2 import service_account
from dotenv import load_dotenv

load_dotenv()

app = FastAPI(swagger_ui_parameters={"persistAuthorization": True})

SCOPES = ['https://www.googleapis.com/auth/spreadsheets', 'https://www.googleapis.com/auth/calendar']
SPREADSHEET_ID = os.environ["SPREADSHEET_ID"]
CALENDAR_ID = os.environ["CALENDAR_ID"]
SERVICE_ACCOUNT_FILE = 'secrets/service.json'

AUTHORIZATION_TOKEN = os.environ["AUTHORIZATION_TOKEN"]

class TokenAuthenticationScheme(HTTPBearer):
    def __init__(self, auto_error: bool = True):
        super(TokenAuthenticationScheme, self).__init__(auto_error=auto_error)

    async def __call__(self, request: Request):
        credentials: HTTPAuthorizationCredentials = await super(TokenAuthenticationScheme, self).__call__(request)
        if not credentials:
            raise HTTPException(status_code=403, detail="Invalid authorization code.")
        
        if not credentials.scheme == "Bearer":
            raise HTTPException(status_code=403, detail="Invalid authentication scheme.")
        
        if not credentials.credentials == AUTHORIZATION_TOKEN:
            raise HTTPException(status_code=403, detail="Invalid token.")
        
        return credentials.credentials

tokenScheme = TokenAuthenticationScheme()
googleCredentials = service_account.Credentials.from_service_account_file(SERVICE_ACCOUNT_FILE, scopes=SCOPES)
googleSheets = build("sheets", "v4", credentials=googleCredentials).spreadsheets()
googleCalendar = build("calendar", "v3", credentials=googleCredentials).events()

locale.setlocale(locale.LC_TIME, "de_DE.UTF-8")

@app.get("/")
def read_root():
    return "WG-API v1.0.0"

@app.get("/mealcount")
def read_mealcount(_: Annotated[str, Depends(tokenScheme)]) -> Tuple[int, int, int, int]:
    result = googleSheets.values().get(spreadsheetId=SPREADSHEET_ID, range="C3:F3").execute()
    values = result.get('values', [])
    return (int(values[0][0]), int(values[0][1]), int(values[0][2]), int(values[0][3]))

@app.get("/mealcount/names")
def read_mealcount_names(_: Annotated[str, Depends(tokenScheme)]) -> Tuple[str, str, str, str]:
    try:
        result = googleSheets.values().get(spreadsheetId=SPREADSHEET_ID, range="C2:F2").execute()
        values = result.get('values', [])
        return (values[0][0], values[0][1], values[0][2], values[0][3])
    except HttpError as e:
        raise HTTPException(status_code=400, detail="Error")

@app.post("/mealcount")
def update_mealcount(to_insert: Tuple[int,int,int,int], _: Annotated[str, Depends(tokenScheme)]):
    print("Request: ", to_insert)
    
    try:
        (p1,p2,p3,p4) = to_insert
    except:
        raise HTTPException(status_code=400, detail="Invalid request")

    try:
        googleSheets.values().update(
            spreadsheetId=SPREADSHEET_ID,
            range="C3:F3",
            valueInputOption="USER_ENTERED",
            body={"values": [[p1,p2,p3,p4]]}
        ).execute()
        
        return
    except HttpError as e:
        raise HTTPException(status_code=500, detail="Internal server Error")


class EventsResponse(BaseModel):
    events: List[str]
    secondsUntilMidnight: int
    day: int
    month: int
    dayString: str

@app.get("/motd")
def message_of_the_day(_: Annotated[str, Depends(tokenScheme)]) -> EventsResponse:
    morning = datetime.datetime.now().astimezone().replace(hour=12, minute=0, second=0)
    evening = datetime.datetime.now().astimezone().replace(hour=13, minute=0, second=0)

    events = googleCalendar.list(calendarId=CALENDAR_ID, singleEvents=True, timeMin=morning.isoformat(), timeMax=evening.isoformat()).execute()

    eventNames = []

    for event in events["items"]:
        #print("{}-{}: {}".format(event["start"]["date"], event["end"]["date"] , event["summary"]))
        eventName = event["summary"]

        if ":" in eventName:
            parts = eventName.split(":")
            eventName = f"{parts[1].strip()}: {parts[0].strip()}"

        eventNames.append(eventName)

    now = datetime.datetime.now().astimezone()
    midnight = datetime.datetime.now().astimezone().replace(hour=23, minute=59, second=59)
    secondsUntilMidnight = (midnight - now).seconds

    return {
        "events": eventNames,
        "day": now.day,
        "month": now.month,
        "dayString": now.strftime("%A"),
        "secondsUntilMidnight": secondsUntilMidnight
    }

def main():
    pass

if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000)
