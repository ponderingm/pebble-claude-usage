var DIRECTION_CODES = {
  'home_to_work': 0,
  'work_to_home': 1
};

var DAY_SUNDAY = 0;
var DAY_SATURDAY = 6;

function dayScheduleFor(timetable, now) {
  var day = now.getDay();
  if (day === DAY_SUNDAY) {
    return timetable.sunday_holiday;
  }
  if (day === DAY_SATURDAY) {
    return timetable.saturday;
  }
  return timetable.weekday;
}

function minutesUntil(departureHHMM, now) {
  var parts = departureHHMM.split(':');
  var departure = new Date(now.getFullYear(), now.getMonth(), now.getDate(),
    parseInt(parts[0], 10), parseInt(parts[1], 10), 0, 0);
  return Math.round((departure.getTime() - now.getTime()) / (60 * 1000));
}

function nextDeparture(timetable, direction, now) {
  var schedule = dayScheduleFor(timetable, now);
  var directionCode = DIRECTION_CODES[direction];
  if (directionCode === undefined) {
    return { directionCode: -1, minutesUntilNext: -1 };
  }
  if (!schedule || !schedule[direction]) {
    return { directionCode: directionCode, minutesUntilNext: -1 };
  }

  var departures = schedule[direction].departures;
  for (var i = 0; i < departures.length; i++) {
    var diff = minutesUntil(departures[i], now);
    if (diff >= 0) {
      return { directionCode: directionCode, minutesUntilNext: diff };
    }
  }
  return { directionCode: directionCode, minutesUntilNext: -1 };
}

module.exports = {
  nextDeparture: nextDeparture
};
