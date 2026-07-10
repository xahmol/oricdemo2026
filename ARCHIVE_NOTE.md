# Archive note: `pt3` branch

This branch is an archival snapshot of the PT3 (Vortex Tracker) music
player work (`include/pt3.c`/`pt3.h`, `docs/pt3.md`, and the real demo's
use of it in `src/main.c`), taken at the point the project pivoted away
from PT3 to an Arkos Tracker (`.aky`) music player instead.

**Status: not fully functional.** Across six rounds of debugging (see
`docs/pt3.md` and the project's own memory notes for the full history),
several real, confirmed decode bugs were found and fixed — ornament-select
stream misalignment, a sticky noise-mixer latch, a doubled sample-select
byte offset, the amplitude nibble read from the wrong sample-step byte,
and a note strike not resetting `sample_pos`/`ornament_pos`. Even after
all of that, and after swapping the demo's tune from `oxygene4.pt3` to
`popcorn.pt3` to sidestep one specific instrument's noise-heavy attack
transient, the music still didn't sound right to the user, and the
player's own runtime performance overhead was judged too high for this
project's needs.

This branch is kept for reference only — do not build on it going
forward. `main` no longer carries this code; see its own history for the
Arkos Tracker player that replaces it.
