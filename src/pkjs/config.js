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
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
