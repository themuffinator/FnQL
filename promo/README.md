# FnQL promo kit

Everything needed to promote FnQL, built around release **0.1.0.158**
(2026-11-16). Start with the plan, read the messaging guide, then post from
the channel folders.

| File | What it's for |
| --- | --- |
| [PLAN.md](PLAN.md) | Goals, baseline numbers, audiences, timeline, risks, metrics |
| [MESSAGING.md](MESSAGING.md) | Pitches, taglines, key messages, proof points and **guardrails**. Every post draws on it |
| [presskit.html](presskit.html) | One-page press kit: open it in a browser, or host it with `images/` |
| **moddb/** | |
| [moddb/PAGE.md](moddb/PAGE.md) | Mod page fields, summary and full description |
| [moddb/ARTICLE-LAUNCH.md](moddb/ARTICLE-LAUNCH.md) | Launch news article, reusable per release |
| [moddb/DOWNLOAD.md](moddb/DOWNLOAD.md) | File mirror listings with install text |
| [moddb/MEDIA.md](moddb/MEDIA.md) | Gallery upload order and captions |
| **steam/** | |
| [steam/DISCUSSION.md](steam/DISCUSSION.md) | Quake Live Community Hub post (Steam BBCode) |
| [steam/GUIDE.md](steam/GUIDE.md) | Steam Guide: install and tune FnQL |
| **reddit/** | |
| [reddit/r-QuakeLive.md](reddit/r-QuakeLive.md) | Launch post with prepared replies |
| [reddit/r-quake.md](reddit/r-quake.md) | Second-wave post, lineage angle |
| [reddit/r-QuakeLive-servers.md](reddit/r-QuakeLive-servers.md) | Server-operator post |
| **social/** | |
| [social/POSTS.md](social/POSTS.md) | Launch thread and feature-spotlight posts |
| [social/DISCORD.md](social/DISCORD.md) | Discord announcements |
| [social/VIDEO.md](social/VIDEO.md) | Trailer shot list, capture recipe, YouTube description |
| **press/** | |
| [press/FACT_SHEET.md](press/FACT_SHEET.md) | Facts, feature list and FAQ |
| [press/OUTREACH.md](press/OUTREACH.md) | Creator outreach template and tracker |
| **images/** | |
| [images/README.md](images/README.md) | Every image, with its size, use and how it was made, including the feature comparisons in `images/features/` |
| **tools/** | |
| [tools/capture_shots.py](tools/capture_shots.py) | Windowless screenshot capture from each map's intermission camera |
| [tools/compose_art.py](tools/compose_art.py) | Builds the key art and banners from screenshots |
| [tools/compose_compare.py](tools/compose_compare.py) | Builds the before/after feature comparisons in `images/features/` |

## Before launch

Open items that only the maintainer can settle are marked `TODO` in the
files. Find them with:

```bash
grep -rn "TODO" promo --include=*.md
```

## Keeping it current

When a release ships, update `MESSAGING.md` ("What's new" and the pinned
version), then copy the new highlights into a fresh ModDB article and a
short social post. Re-capture screenshots when a renderer changes
noticeably; `images/README.md` records how.
