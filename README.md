# Cal Pop (3DS)

The colorful edition of Cal Tracker 3DS. It shares its data folder with the plain version, so both apps see the same log.

A standalone calorie and macro tracker for the Nintendo 3DS (homebrew). It keeps its own food log on the SD card and looks foods up on [Open Food Facts](https://world.openfoodfacts.org). It needs no account and no server.

## Features

- Daily view of calories, protein, carbs and fat against your own targets.
- Log foods to Breakfast, Lunch, Dinner or Snacks, with grams.
- Online food search through Open Food Facts.
- Saved foods: everything you log is cached, so re-logging works offline.
- Custom foods entered per serving.
- Browse, edit and delete past days.

## Cal Pop extras

- One color gradient everywhere: green, turquoise, blue.
- Touch tab bar on the bottom screen: Today, Search, Brands, Favs, Meals, Profile (L and R also switch tabs).
- **Favorites:** press X on any food to star it; on the Favs tab, A logs it with its usual grams.
- **Quick add:** type a name with calories and macros, no grams needed.
- **Saved meals:** on Meals, tap *+ New meal* and name it. Then pick foods the same way you log them: A on Search, Favs or Brands adds the food to the meal instead of your day. *Quick item* adds a name with macros. Tap the strip at the bottom to go back and *Save meal*. Log it later from the Meals tab, normal size or scaled.
- **My Brands:** follow brands like Quest or Barebells and browse their products. Small typos are fixed for you (typing "goodless" finds Goodles and renames the tile). Product lists are saved for offline use; Y refreshes.
- **Profile** (Left/Right switch pages):
  - *Profile:* age, sex, weight log, height, BMI, body fat and your daily targets.
  - *Activity:* today's steps straight from the 3DS step counter (with an hourly chart), and your workouts. Pick a workout type (weights, running, soccer...) and the minutes; calories come from your weight. *Other* lets you type the calories yourself.
  - *Report:* eaten vs burned for the day. Burned is resting energy (Mifflin-St Jeor BMR) plus steps plus workouts.
  - *Charts:* calories per day for the week, split into protein, carbs and fat, and a weight line.
- Steps are worth about 0.5 kcal per kg per km walked, with your stride worked out from your height. Workouts use MET values from the Compendium of Physical Activities, minus resting energy so nothing is counted twice.
- HOME menu banner: one macaroni with a short jingle (CIA only; made by `tools/make_assets.py`).
- "Adding to" picker on the bottom screen (or Left/Right) chooses the meal food goes into.

## Controls (plain version)

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

- `cal-pop-cia`: a CIA to install with FBI, or to open in an emulator (File > Install CIA).
- `cal-pop`: a `.3dsx` for the Homebrew Launcher; copy it to `sdmc:/3ds/`.

Data lives in `sdmc:/3ds/caltracker/`: `log/YYYY-MM-DD.json` per day, `foods.json` for saved foods, `targets.json` for your targets, plus Cal Pop's `favorites.json`, `meals.json`, `brands.json`, `profile.json`, `weights.json`, `steps.json` and `workouts.json`.

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
