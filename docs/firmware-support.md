# Running PS5VR on other firmware

PS5VR currently ships an export table for **13.60** only. This page is for
anyone on another firmware who wants theirs supported, and explains why it
cannot be done from a 13.60 console.

## Why there is a table at all

The PS VR2 system libraries (`libSceHmd2`, `libSceVrTracker2`, `libSceVrHand`)
keep the functions PS5VR needs out of their dynamic symbol tables, so there is
nothing to `sceKernelDlsym`. The app calls them at fixed offsets inside the
library instead, and those offsets are specific to one build of one firmware.

Nothing bad happens on the wrong firmware. Each table carries an *anchor*: a
string that must sit at a known offset in the library's read-only data. If it
does not match, the offsets are not used and PS5VR stops instead of calling
into the wrong place.

## Why a 13.60 console cannot produce another firmware's table

The offsets are read out of the decrypted library itself. A console only has
its own firmware's copy, so a 13.60 machine can only ever generate the 13.60
table. Supporting 12.40 needs the library from a 12.40 console.

## What is needed from you

Three decrypted libraries from a console on your firmware:

- `libSceHmd2.sprx`
- `libSceVrTracker2.sprx`
- `libSceVrHand.sprx`

They live in `/system/common/lib/` and are encrypted on disk. Decrypt them
with `ps5-self-pager`, which is what the tooling here expects as input.

Open an issue saying which firmware you are on and attach them, or the
generated table if you would rather run the generator yourself:

```sh
python3 tools/gen_export_table.py \
    libSceHmd2.sprx      sceHmd2Initialize sceHmd2Open ... \
    -- libSceVrTracker2.sprx  sceVrTracker2Init ... \
    -- libSceVrHand.sprx      sceVrHandInit ... \
  > app/src/vr_exports_<fw>.h
```

Run `python3 tools/ps5_dynlib.py <lib.sprx>` first to list what a library
exports on your firmware. The names PS5VR needs are the ones already listed in
`app/src/vr_exports_1360.h`.

Please only send libraries from your own console.

## Then what

The app picks its table by anchor, so adding one is additive: a new firmware
cannot affect 13.60. Once a table is in, the only remaining work is a run
through the library and playback on that firmware to confirm nothing else
moved.
