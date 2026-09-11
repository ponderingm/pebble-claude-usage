var weatherCodes = require('./weather_codes.json');

var WEATHER_API_URL = 'https://api.open-meteo.com/v1/forecast';

var SHAPE_IDS = {
  'sun': 0,
  'cloud_sun': 1,
  'cloud': 2,
  'fog': 3,
  'rain': 4,
  'snow': 5,
  'storm': 6
};
var SHAPE_ID_UNKNOWN = 7;

function xhrRequest(url, callback) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    callback(null, xhr.status, xhr.responseText);
  };
  xhr.onerror = function () {
    callback(new Error('network error for ' + url));
  };
  xhr.open('GET', url);
  xhr.send();
}

function shapeIdForCode(wmoCode) {
  var entry = weatherCodes[String(wmoCode)];
  if (!entry) {
    return SHAPE_ID_UNKNOWN;
  }
  var shapeId = SHAPE_IDS[entry.shape];
  return shapeId === undefined ? SHAPE_ID_UNKNOWN : shapeId;
}

function fetchWeather(lat, lon, callback) {
  var url = WEATHER_API_URL +
    '?latitude=' + encodeURIComponent(lat) +
    '&longitude=' + encodeURIComponent(lon) +
    '&current=temperature_2m,weather_code';

  xhrRequest(url, function (err, status, responseText) {
    if (err) {
      return callback(err);
    }
    if (status !== 200) {
      return callback(new Error('weather request failed, status ' + status));
    }
    var json;
    try {
      json = JSON.parse(responseText);
    } catch (e) {
      return callback(new Error('weather response was invalid JSON'));
    }
    var current = json.current || {};
    if (typeof current.temperature_2m !== 'number') {
      return callback(new Error('weather response missing temperature_2m'));
    }
    callback(null, {
      tempC: Math.round(current.temperature_2m),
      shapeId: shapeIdForCode(current.weather_code)
    });
  });
}

module.exports = {
  fetchWeather: fetchWeather
};
