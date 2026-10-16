#pragma once

// Web UI (basic auth) and OTA. Runs in the loop task: webBegin() once Wi-Fi is up,
// webPoll() every loop. Commands go to the control task; nothing here touches hardware.
void webBegin();
void webPoll();
