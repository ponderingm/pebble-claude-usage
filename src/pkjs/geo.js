var GEO_TIMEOUT_MS = 20 * 1000;
var GEO_MAX_AGE_MS = 5 * 60 * 1000;
var AMBIGUOUS_DISTANCE_MARGIN_M = 300;
var FALLBACK_AFTERNOON_HOUR = 12;
var EARTH_RADIUS_M = 6371000;

function getCurrentPosition(callback) {
  if (!navigator.geolocation) {
    return callback(new Error('geolocation not available'));
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) {
      callback(null, pos.coords.latitude, pos.coords.longitude);
    },
    function (err) {
      callback(new Error('geolocation failed: ' + err.message));
    },
    { timeout: GEO_TIMEOUT_MS, maximumAge: GEO_MAX_AGE_MS }
  );
}

function toRadians(deg) {
  return (deg * Math.PI) / 180;
}

function haversineMeters(lat1, lon1, lat2, lon2) {
  var dLat = toRadians(lat2 - lat1);
  var dLon = toRadians(lon2 - lon1);
  var a =
    Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(toRadians(lat1)) * Math.cos(toRadians(lat2)) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);
  var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return EARTH_RADIUS_M * c;
}

function pickDirection(lat, lon, homeLat, homeLon, workLat, workLon, now) {
  var distHome = haversineMeters(lat, lon, homeLat, homeLon);
  var distWork = haversineMeters(lat, lon, workLat, workLon);

  if (Math.abs(distHome - distWork) < AMBIGUOUS_DISTANCE_MARGIN_M) {
    var hour = (now || new Date()).getHours();
    return hour < FALLBACK_AFTERNOON_HOUR ? 'home_to_work' : 'work_to_home';
  }
  return distHome < distWork ? 'home_to_work' : 'work_to_home';
}

module.exports = {
  getCurrentPosition: getCurrentPosition,
  haversineMeters: haversineMeters,
  pickDirection: pickDirection
};
