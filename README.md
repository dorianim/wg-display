# WG display

A simple display used in our shared flat to display upcoming events and keep track of expenses. Based on an [esp32](https://www.az-delivery.de/en/products/esp32-lolin-lolin32) with an [e-ink display](https://www.waveshare.com/2.7inch-e-paper-hat.htm). The esp goes to sleep after a few seconds and only wakes up when a button is pressed and at 01:00 am to refresh upcoming events. The api is deployed on Google cloud and uses Google APIs to push meal counts to out google sheet and pull events from our calendar.

# Pictures

<table align="center">
    <tr>
        <td align="center">
            <img src="https://github.com/dorianim/wg-display/blob/main/.github/images/counts.jpeg?raw=true" width="300px" />
          <br>
          Counts
        </td>
        <td align="center">
            <img src="https://github.com/dorianim/wg-display/blob/main/.github/images/events.jpeg?raw=true" width="300px" />
          <br>
          Events (during sleep)
        </td>
    </tr>
</table>

## Build

- Create config.h `cp config.h.example config.h`
- Fill in you Wi-Fi and api credentials
- Build using PlatformIO `pio build`
