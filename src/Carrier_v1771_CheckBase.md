# v1.7.71: carrier checked on load

- "Grete": the hidden carrier in a save could be a townsperson (an earlier test build of v1.7.63 picked one before the
  mannequin). Only horses were swapped.
- TendCarrier now logs once a load "Carrier: <id> is <name> (<base>), wanted <name> (<base>)" and swaps a carrier whose
  base is not FindCarrierBase() (the mannequin), once a load (carrierSwapped_: no respawn loop if a base never
  matches). Dead and horse carriers as before.
