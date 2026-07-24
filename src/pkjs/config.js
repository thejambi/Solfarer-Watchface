// Clay settings page — generated locally by pebble-clay, no hosted page.
// Select values are string ints matching the enums in src/c/settings.h.
module.exports = [
  {
    type: 'heading',
    defaultValue: 'Lighthaul Watchface'
  },
  {
    type: 'text',
    defaultValue: 'Your ship flies itself. A new star map every day; a contract flown at every bell.'
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Operations' },
      {
        type: 'select',
        messageKey: 'FaceMode',
        defaultValue: '0',
        label: 'Face mode',
        description: 'Chart only hides the economy — no pay, credits, or fuel. The map still lives: ports rotate every minute, the ship hops at every bell, and the freed text shows your steps, distance, and heart rate.',
        options: [
          { label: 'Full game', value: '0' },
          { label: 'Chart only', value: '1' }
        ]
      },
      {
        type: 'select',
        messageKey: 'Cadence',
        defaultValue: '0',
        label: 'Run a contract',
        options: [
          { label: 'Every hour', value: '0' },
          { label: 'Every 30 minutes', value: '1' }
        ]
      },
      {
        type: 'select',
        messageKey: 'Dispatch',
        defaultValue: '1',
        label: 'Dispatch',
        description: 'Which offer the ship flies at the bell. Every port carries a contract each window; "Selected" is whatever the minute rotation shows.',
        options: [
          { label: 'Selected, no matter what', value: '0' },
          { label: 'Selected, fall back if doomed', value: '1' },
          { label: 'Best pay', value: '2' },
          { label: 'Safest', value: '3' }
        ]
      },
      {
        type: 'select',
        messageKey: 'Cutscene',
        defaultValue: '0',
        label: 'On each run, show',
        options: [
          { label: 'Flight + results', value: '0' },
          { label: 'Results card only', value: '1' },
          { label: 'Nothing (stay on map)', value: '2' }
        ]
      },
      {
        type: 'select',
        messageKey: 'UpgradeStrategy',
        defaultValue: '0',
        label: 'Auto-buy upgrades',
        description: 'Shops the two upgrades stocked at each dock, always keeping a full refuel in reserve.',
        options: [
          { label: 'Priority order', value: '0' },
          { label: 'Cheapest first', value: '1' },
          { label: 'Random', value: '2' },
          { label: 'Never', value: '3' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Star map' },
      {
        type: 'select',
        messageKey: 'SeedMode',
        defaultValue: '0',
        label: 'Map',
        description: 'Daily: a fresh map and career each day, shared by every pilot. Fixed: one persistent map and career.',
        options: [
          { label: 'New map daily', value: '0' },
          { label: 'Fixed seed', value: '1' }
        ]
      },
      {
        type: 'input',
        messageKey: 'FixedSeed',
        defaultValue: '',
        label: 'Fixed seed',
        description: 'Up to 9 characters. Blank falls back to the daily map.',
        attributes: { placeholder: 'e.g. zanzibar', limit: 9 }
      },
      {
        type: 'toggle',
        messageKey: 'ShowSignposts',
        defaultValue: true,
        label: 'Deep-space signposts',
        description: 'Edge markers pointing at licensed deep-space stations.'
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
        label: 'Comms-lost indicator',
        description: 'Shows BT! when the phone link drops.'
      },
      {
        type: 'toggle',
        messageKey: 'TapInfo',
        defaultValue: true,
        label: 'Tap for info overlay',
        description: 'Tap or flick the wrist to pop a stats panel over the map.'
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
        type: 'select',
        messageKey: 'VibeMode',
        defaultValue: '0',
        label: 'Vibrate on runs',
        description: 'Always silent during Quiet Time.',
        options: [
          { label: 'Never', value: '0' },
          { label: 'Every delivery', value: '1' },
          { label: 'Records & license only', value: '2' }
        ]
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
