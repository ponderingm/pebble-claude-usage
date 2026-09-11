module.exports = [
  {
    "type": "heading",
    "defaultValue": "Claude Usage Settings"
  },
  {
    "type": "text",
    "defaultValue": "Paste your Claude Code OAuth refresh token. Get it via `claude setup-token`, or copy the refreshToken field from ~/.claude/.credentials.json. This value stays on your phone and is never shared with anyone else."
  },
  {
    "type": "input",
    "messageKey": "refresh_token",
    "label": "OAuth Refresh Token",
    "attributes": {
      "type": "password",
      "placeholder": "sk-ant-ort01-..."
    },
    "defaultValue": ""
  },
  {
    "type": "select",
    "messageKey": "poll_interval_minutes",
    "label": "Refresh Interval",
    "defaultValue": "15",
    "options": [
      { "label": "5 minutes", "value": "5" },
      { "label": "15 minutes", "value": "15" },
      { "label": "30 minutes", "value": "30" },
      { "label": "60 minutes", "value": "60" }
    ]
  },
  {
    "type": "heading",
    "defaultValue": "Weather"
  },
  {
    "type": "toggle",
    "messageKey": "weather_enabled",
    "label": "Show Weather",
    "defaultValue": true
  },
  {
    "type": "heading",
    "defaultValue": "Commute Bus"
  },
  {
    "type": "text",
    "defaultValue": "Toei Bus route Gyo-10 (Honjo-yonchome <-> Kiba-yonchome). Enter your home and work coordinates (decimal degrees) so the watch can tell which direction you are commuting and show the next departure."
  },
  {
    "type": "toggle",
    "messageKey": "bus_enabled",
    "label": "Show Next Bus",
    "defaultValue": true
  },
  {
    "type": "input",
    "messageKey": "home_lat",
    "label": "Home Latitude",
    "attributes": {
      "type": "text",
      "placeholder": "35.7100"
    },
    "defaultValue": ""
  },
  {
    "type": "input",
    "messageKey": "home_lon",
    "label": "Home Longitude",
    "attributes": {
      "type": "text",
      "placeholder": "139.8180"
    },
    "defaultValue": ""
  },
  {
    "type": "input",
    "messageKey": "work_lat",
    "label": "Work Latitude",
    "attributes": {
      "type": "text",
      "placeholder": "35.6720"
    },
    "defaultValue": ""
  },
  {
    "type": "input",
    "messageKey": "work_lon",
    "label": "Work Longitude",
    "attributes": {
      "type": "text",
      "placeholder": "139.8080"
    },
    "defaultValue": ""
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
