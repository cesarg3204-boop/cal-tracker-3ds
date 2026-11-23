# Cal Tracker 3DS

A standalone calorie and macro tracker for the Nintendo 3DS (homebrew). It keeps its own food log on the SD card and looks foods up on [Open Food Facts](https://world.openfoodfacts.org). It needs no account and no server.

## Features

- Daily view of calories, protein, carbs and fat against your own targets.
- Log foods to Breakfast, Lunch, Dinner or Snacks, with grams.
- Online food search through Open Food Facts.
- Saved foods: everything you log is cached, so re-logging works offline.
- Custom foods entered per serving.
- Browse, edit and delete past days.

## Controls

| Screen | Button | Action |
| --- | --- | --- |
| Day | A | Add food |
| Day | X / Y | Remove entry / change grams |
| Day | Left, Right (or L, R) | Previous / next day |
| Day | B | Jump back to today |
| Day | SELECT | Set daily targets |
| Add food | A | Log the selected saved food |
| Add food | Y | Search online |
| Add food | X | Create a custom food |
| Add food, results | L, R | Change meal |
| Any | START | Quit |

## Install

Each **Build** run on the Actions tab has two downloads:

- `cal-tracker-3ds-cia`: a CIA to install with FBI, or to open in an emulator (File > Install CIA).
- `cal-tracker-3ds`: a `.3dsx` for the Homebrew Launcher; copy it to `sdmc:/3ds/`.

Data lives in `sdmc:/3ds/caltracker/`: `log/YYYY-MM-DD.json` per day, `foods.json` for saved foods and `targets.json` for your targets.

## Build

With [devkitPro](https://devkitpro.org/wiki/Getting_Started) installed:

```sh
dkp-pacman -S 3ds-dev 3ds-curl 3ds-mbedtls 3ds-zlib
make
```

The portable core (log storage, nutrient maths, Open Food Facts parsing) has tests that run on a PC:

```sh
make -f Makefile.tests
```

## Credits

- Food data from [Open Food Facts](https://world.openfoodfacts.org) (ODbL).
- [cJSON](https://github.com/DaveGamble/cJSON) (MIT), vendored in `source/lib`.
- CA bundle in `romfs/cacert.pem` from [certifi](https://github.com/certifi/python-certifi) (MPL 2.0).
