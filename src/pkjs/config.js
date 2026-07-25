// Clay settings page — generated locally, no hosted page.
// Select values are string ints matching the enums in src/c/settings.h.
var HOURS = [];
for (var h = 0; h < 24; h++) {
  var label = h === 0 ? 'Midnight' : h === 12 ? 'Noon'
            : h < 12 ? h + ' AM' : (h - 12) + ' PM';
  HOURS.push({ label: label, value: String(h) });
}

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Solfarer'
  },
  {
    type: 'text',
    defaultValue: 'Wander the real stars. Each day starts at Sol; every bell your ship hops to a nearby star — Proxima, Barnard’s Star, Wolf 359, and 2,200 more of the Local Bubble.'
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'The wander' },
      {
        type: 'toggle',
        messageKey: 'ShowHealth',
        defaultValue: true,
        label: 'Health data',
        description: 'Steps, distance, and heart rate beside the clock, plus the past-hour activity sparkline. A shake shows hours slept. Turn off for a pure star chart.'
      },
      {
        type: 'select',
        messageKey: 'Cadence',
        defaultValue: '0',
        label: 'Hop',
        options: [
          { label: 'Every hour', value: '0' },
          { label: 'Every 30 minutes', value: '1' }
        ]
      },
      {
        type: 'select',
        messageKey: 'StartHour',
        defaultValue: '7',
        label: 'Day starts at Sol at',
        description: 'The wander day is anchored here — set it to when you wake and you’ll see home before the ship leaves.',
        options: HOURS
      },
      {
        type: 'select',
        messageKey: 'Cutscene',
        defaultValue: '0',
        label: 'On each hop, show',
        options: [
          { label: 'Flight + arrival card', value: '0' },
          { label: 'Nothing (stay on map)', value: '1' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Time & display' },
      {
        type: 'select',
        messageKey: 'DateFormat',
        defaultValue: '0',
        label: 'Date',
        options: [
          { label: 'Weekday + day', value: '0' },
          { label: 'Month + day', value: '1' },
          { label: 'Off', value: '2' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'LeadingZero',
        defaultValue: false,
        label: 'Leading zero (12h clock)'
      },
      {
        type: 'toggle',
        messageKey: 'ShowBattery',
        defaultValue: true,
        label: 'Battery gauge'
      },
      {
        type: 'toggle',
        messageKey: 'ShowBT',
        defaultValue: true,
        label: 'Comms-lost indicator'
      },
      {
        type: 'toggle',
        messageKey: 'TapInfo',
        defaultValue: true,
        label: 'Shake gesture',
        description: 'Browse the neighbor stars (display only — the bell still flies its own pick). In health mode a shake also shows hours slept for a few seconds.'
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Weather' },
      {
        type: 'toggle',
        messageKey: 'WeatherOn',
        defaultValue: false,
        label: 'Current temperature',
        description: 'Bottom-right corner, from Open-Meteo, refreshed every 30 minutes. Off by default: turning it on is what triggers the phone-location prompt — unless a manual location is set below.'
      },
      {
        type: 'input',
        messageKey: 'WeatherLoc',
        defaultValue: '',
        label: 'Location (city or postal code)',
        description: 'When filled in, weather uses this place and never touches phone location.',
        attributes: { placeholder: 'e.g. Minneapolis or 55401' }
      },
      {
        type: 'select',
        messageKey: 'WeatherUnit',
        defaultValue: '0',
        label: 'Units',
        options: [
          { label: 'Fahrenheit', value: '0' },
          { label: 'Celsius', value: '1' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Alerts' },
      {
        type: 'toggle',
        messageKey: 'HopVibe',
        defaultValue: false,
        label: 'Vibrate on each hop',
        description: 'Always silent during Quiet Time.'
      },
      {
        type: 'toggle',
        messageKey: 'BTVibe',
        defaultValue: false,
        label: 'Vibrate on comms loss'
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
