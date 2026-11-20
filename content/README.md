# Picture packs

Each folder here becomes a pack that screens can download from their web page
("Get the latest Pocket Jumbotron pictures"). Push to `main` and GitHub Actions publishes it.

**Everything here is public** (the repo and the website). Only your own or royalty-free pictures.
Anything private goes in the private repo `hogfanboy/pocket-jumbotron-content` (same layout, plus `codes.json`):
the website only gets encrypted copies, which a screen opens with its family code.

```
content/
  christmas-2026/          folder name = pack id (lowercase, digits, dashes)
    pack.json              optional, see below
    01-tree.webp           pictures: webp, gif, png, jpg (animated is fine), any size
    02-snowman.gif
  smith-family/
    pack.json              { "title": "For the Smiths", "cards": ["PocketJumbo-EED0"] }
    happy-birthday.webp
```

`pack.json` fields (all optional):
- `title`: shown on the screen's web page (default: the folder name).
- `about`: one line under the title.
- `cards`: only these screens see the pack (the name shown on the screen, e.g. `PocketJumbo-EED0`).
- `order`: lower numbers are listed first (default 50).

Folders starting with `_` are skipped. Files play in name order, so number them.

Pictures put straight into `content/` (not in a folder) become the "New pictures" pack, listed first.
That is the easy way when uploading on github.com.
