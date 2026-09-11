var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

var geo = require('./geo');
var weather = require('./weather');
var bus = require('./bus');
var busTimetable = require('./bus_timetable.json');

var TOKEN_URL = 'https://console.anthropic.com/v1/oauth/token';
var USAGE_URL = 'https://api.anthropic.com/api/oauth/usage';
var CLIENT_ID = '9d1c250a-e61b-44d9-88ed-5944d1962f5e';
var BETA_HEADER = 'oauth-2025-04-20';
var TOKEN_REFRESH_MARGIN_MS = 5 * 60 * 1000;
var DEFAULT_POLL_MINUTES = 15;

var LS_REFRESH_TOKEN = 'claude_refresh_token';
var LS_ACCESS_TOKEN = 'claude_access_token';
var LS_ACCESS_TOKEN_EXPIRES_AT = 'claude_access_token_expires_at';
var LS_POLL_INTERVAL_MINUTES = 'claude_poll_interval_minutes';
var LS_WEATHER_ENABLED = 'claude_weather_enabled';
var LS_BUS_ENABLED = 'claude_bus_enabled';
var LS_HOME_LAT = 'claude_home_lat';
var LS_HOME_LON = 'claude_home_lon';
var LS_WORK_LAT = 'claude_work_lat';
var LS_WORK_LON = 'claude_work_lon';

var s_poll_timer = null;

function xhrRequest(url, type, headers, body, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(null, xhr.status, xhr.responseText);
  };
  xhr.onerror = function () {
    callback(new Error('network error for ' + url));
  };
  xhr.open(type, url);
  for (var key in headers) {
    if (headers.hasOwnProperty(key)) {
      xhr.setRequestHeader(key, headers[key]);
    }
  }
  xhr.send(body);
}

function refreshAccessToken(refreshToken, callback) {
  var body = JSON.stringify({
    grant_type: 'refresh_token',
    refresh_token: refreshToken,
    client_id: CLIENT_ID
  });

  xhrRequest(TOKEN_URL, 'POST', { 'Content-Type': 'application/json' }, body,
    function (err, status, responseText) {
      if (err) {
        return callback(err);
      }
      if (status !== 200) {
        return callback(new Error('token refresh failed, status ' + status));
      }
      var json;
      try {
        json = JSON.parse(responseText);
      } catch (e) {
        return callback(new Error('token refresh returned invalid JSON'));
      }
      if (!json.access_token) {
        return callback(new Error('token refresh response missing access_token'));
      }

      localStorage.setItem(LS_ACCESS_TOKEN, json.access_token);
      localStorage.setItem(LS_ACCESS_TOKEN_EXPIRES_AT,
        String(Date.now() + (json.expires_in || 0) * 1000));
      if (json.refresh_token) {
        localStorage.setItem(LS_REFRESH_TOKEN, json.refresh_token);
      }

      callback(null, json.access_token);
    }
  );
}

function getValidAccessToken(callback) {
  var refreshToken = localStorage.getItem(LS_REFRESH_TOKEN);
  if (!refreshToken) {
    return callback(new Error('no refresh token configured yet'));
  }

  var cachedToken = localStorage.getItem(LS_ACCESS_TOKEN);
  var expiresAt = parseInt(localStorage.getItem(LS_ACCESS_TOKEN_EXPIRES_AT) || '0', 10);
  if (cachedToken && Date.now() < expiresAt - TOKEN_REFRESH_MARGIN_MS) {
    return callback(null, cachedToken);
  }

  refreshAccessToken(refreshToken, callback);
}

function fetchUsage(accessToken, callback) {
  var headers = {
    'Authorization': 'Bearer ' + accessToken,
    'anthropic-beta': BETA_HEADER
  };

  xhrRequest(USAGE_URL, 'GET', headers, null, function (err, status, responseText) {
    if (err) {
      return callback(err);
    }
    if (status !== 200) {
      return callback(new Error('usage request failed, status ' + status));
    }
    var json;
    try {
      json = JSON.parse(responseText);
    } catch (e) {
      return callback(new Error('usage response was invalid JSON'));
    }
    callback(null, json);
  });
}

function epochSecondsFromIso(isoString) {
  if (!isoString) {
    return 0;
  }
  var parsed = Date.parse(isoString);
  if (isNaN(parsed)) {
    return 0;
  }
  return Math.floor(parsed / 1000);
}

function sendUsageToWatch(usage) {
  var fiveHour = usage.five_hour || {};
  var sevenDay = usage.seven_day || {};

  var dict = {
    'FIVE_HOUR_PCT': Math.round(fiveHour.utilization || 0),
    'FIVE_HOUR_RESET_EPOCH': epochSecondsFromIso(fiveHour.resets_at),
    'SEVEN_DAY_PCT': Math.round(sevenDay.utilization || 0),
    'SEVEN_DAY_RESET_EPOCH': epochSecondsFromIso(sevenDay.resets_at)
  };

  Pebble.sendAppMessage(dict,
    function () {
      console.log('Claude usage sent to watch: ' + JSON.stringify(dict));
    },
    function () {
      console.log('Claude usage: failed to send AppMessage');
    }
  );
}

function pollAndSend() {
  getValidAccessToken(function (err, accessToken) {
    if (err) {
      console.log('Claude usage: ' + err.message);
      return;
    }
    fetchUsage(accessToken, function (err2, usage) {
      if (err2) {
        console.log('Claude usage: ' + err2.message);
        return;
      }
      sendUsageToWatch(usage);
    });
  });
}

function parseBoolean(rawValue, defaultValue) {
  if (rawValue === null || rawValue === undefined || rawValue === '') {
    return defaultValue;
  }
  return rawValue === 'true' || rawValue === true;
}

function sendWeatherAndBusToWatch(weatherResult, busResult) {
  var dict = {};
  if (weatherResult) {
    dict['WEATHER_TEMP_C'] = weatherResult.tempC;
    dict['WEATHER_CODE'] = weatherResult.shapeId;
  }
  if (busResult) {
    dict['BUS_DIRECTION'] = busResult.directionCode;
    dict['BUS_NEXT_MIN'] = busResult.minutesUntilNext;
  }
  if (Object.keys(dict).length === 0) {
    return;
  }
  Pebble.sendAppMessage(dict,
    function () {
      console.log('Weather/bus sent to watch: ' + JSON.stringify(dict));
    },
    function () {
      console.log('Weather/bus: failed to send AppMessage');
    }
  );
}

function pollWeatherAndBus() {
  var weatherEnabled = parseBoolean(localStorage.getItem(LS_WEATHER_ENABLED), true);
  var busEnabled = parseBoolean(localStorage.getItem(LS_BUS_ENABLED), true);
  if (!weatherEnabled && !busEnabled) {
    return;
  }

  var homeLat = parseFloat(localStorage.getItem(LS_HOME_LAT));
  var homeLon = parseFloat(localStorage.getItem(LS_HOME_LON));
  var workLat = parseFloat(localStorage.getItem(LS_WORK_LAT));
  var workLon = parseFloat(localStorage.getItem(LS_WORK_LON));
  var hasHomeWork = !isNaN(homeLat) && !isNaN(homeLon) && !isNaN(workLat) && !isNaN(workLon);

  geo.getCurrentPosition(function (err, lat, lon) {
    if (err) {
      console.log('Claude usage: ' + err.message);
      return;
    }

    var weatherResult = null;
    var busResult = null;
    var pendingCount = 0;

    function sendIfDone() {
      if (pendingCount === 0) {
        sendWeatherAndBusToWatch(weatherResult, busResult);
      }
    }

    if (busEnabled && hasHomeWork) {
      var direction = geo.pickDirection(lat, lon, homeLat, homeLon, workLat, workLon);
      busResult = bus.nextDeparture(busTimetable, direction, new Date());
    }

    if (weatherEnabled) {
      pendingCount++;
      weather.fetchWeather(lat, lon, function (werr, result) {
        if (werr) {
          console.log('Claude usage: ' + werr.message);
        } else {
          weatherResult = result;
        }
        pendingCount--;
        sendIfDone();
      });
    }

    sendIfDone();
  });
}

function pollAll() {
  pollAndSend();
  pollWeatherAndBus();
}

function schedulePolling() {
  if (s_poll_timer) {
    clearInterval(s_poll_timer);
  }
  var minutes = parseInt(
    localStorage.getItem(LS_POLL_INTERVAL_MINUTES) || String(DEFAULT_POLL_MINUTES), 10);
  s_poll_timer = setInterval(pollAll, minutes * 60 * 1000);
}

Pebble.addEventListener('ready', function () {
  console.log('Claude usage PebbleKit JS ready');
  schedulePolling();
  pollAll();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload['REQUEST_UPDATE']) {
    pollAll();
  }
});

Pebble.addEventListener('webviewclosed', function (e) {
  if (!e.response) {
    return;
  }
  var settings = clay.getSettings(e.response, false);
  if (settings.refresh_token && settings.refresh_token.value) {
    localStorage.setItem(LS_REFRESH_TOKEN, settings.refresh_token.value);
    localStorage.removeItem(LS_ACCESS_TOKEN);
    localStorage.removeItem(LS_ACCESS_TOKEN_EXPIRES_AT);
  }
  if (settings.poll_interval_minutes && settings.poll_interval_minutes.value) {
    localStorage.setItem(LS_POLL_INTERVAL_MINUTES, settings.poll_interval_minutes.value);
  }
  if (settings.weather_enabled) {
    localStorage.setItem(LS_WEATHER_ENABLED, String(settings.weather_enabled.value));
  }
  if (settings.bus_enabled) {
    localStorage.setItem(LS_BUS_ENABLED, String(settings.bus_enabled.value));
  }
  if (settings.home_lat && settings.home_lat.value) {
    localStorage.setItem(LS_HOME_LAT, settings.home_lat.value);
  }
  if (settings.home_lon && settings.home_lon.value) {
    localStorage.setItem(LS_HOME_LON, settings.home_lon.value);
  }
  if (settings.work_lat && settings.work_lat.value) {
    localStorage.setItem(LS_WORK_LAT, settings.work_lat.value);
  }
  if (settings.work_lon && settings.work_lon.value) {
    localStorage.setItem(LS_WORK_LON, settings.work_lon.value);
  }
  schedulePolling();
  pollAll();
});
