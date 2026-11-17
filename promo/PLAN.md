# FnQL Promotion Plan

Campaign for release **0.1.0.158** (2026-11-16) and the releases after it.
Owner: project maintainer. Drafted 2026-11-17.

## 1. Goals

1. **Players:** get Quake Live players to try FnQL as their everyday client.
2. **Server operators:** get FnQL dedicated servers running publicly, so
   retail and FnQL players meet on them.
3. **Contributors and testers:** bring in bug reports, retail-parity evidence,
   renderer testing on varied GPUs, and code contributions.
4. **Credibility:** be known as the careful, compatibility-first Quake Live
   engine, not "another sourceport fork".

## 2. Baseline (measured 2026-11-17)

| Metric | Value |
| --- | --- |
| FnQL GitHub stars | 18 |
| FnQL-game GitHub stars | 0 |
| 0.1.0.158 downloads (day 1) | 8 packages (3 msvc, 3 mingw, 2 Linux) |
| 0.1.0.134 downloads (lifetime) | 27 packages |
| 0.1.0.117 downloads (lifetime) | 31 packages |
| ModDB / Reddit / Steam presence | none yet |

Re-measure with:

```bash
gh api repos/themuffinator/FnQL/releases --jq ".[0:3][] | .tag_name + \" \" + ([.assets[] | select(.name | test(\"zip|tar\")) | .download_count] | add | tostring)"
```

## 3. Targets (60 days, to 2027-01-16)

Ambitious but reachable for a niche, high-quality project:

| Metric | Target |
| --- | --- |
| GitHub stars (FnQL) | 150+ |
| Package downloads per release | 250+ |
| ModDB watchers | 100+ |
| Public FnQL servers listed in the Steam browser | 3+ |
| New contributors or regular testers | 5+ |
| Issues with retail-evidence reports | 10+ |

## 4. Audiences and what each one needs to hear

| Audience | Where they are | What they care about | Lead message |
| --- | --- | --- | --- |
| Active QL players | Steam QL hub, r/QuakeLive, QL Discords, pickup communities | Feel, FPS, high-refresh, not breaking anything, "will it work on my servers?" | Same game, same servers, better engine |
| Server operators | QL admin Discords, r/QuakeLive, GitHub | Stability, security, Linux hosting, stats, retail clients still joining | Hardened server; retail clients join; stats journal |
| Quake veterans and sourceport fans | r/quake, ModDB, Quake3e/ioq3 circles | Lineage, renderers, faithfulness | From id's GPL source to Quake Live, with Quake3e's speed |
| Developers and reverse engineers | GitHub, r/programming, Hacker News, dev Discords | How retail parity is proven | Lockstep harness, zero-divergence gates, clean-room modules |
| Content creators | YouTube, Twitch | Something fresh to show; capture tools | Better visuals, windowless capture, FFmpeg piping |

## 5. Channels and assets

| Channel | Asset in this folder | Notes |
| --- | --- | --- |
| ModDB | `moddb/` | Mod page under Quake Live, launch article, file listing, gallery. New pages wait for moderation; submit at least two days before launch day. |
| Steam | `steam/` | Quake Live Community Hub discussion post and a Steam Guide. Read the hub's rules first; link to GitHub, don't re-host files. |
| Reddit | `reddit/` | r/QuakeLive first, r/quake a few days later with a different angle. Stay in the comments for the first two hours. |
| Social | `social/` | Bluesky / X / Mastodon thread, Discord announcement, YouTube description. |
| Video | `social/VIDEO.md` | 60–90 s trailer shot list; captured headlessly with FnQL's own `video` piping. |
| Press / creators | `press/` | Fact sheet and outreach email. |
| GitHub | repo settings | Upload `.github/social-preview.png` as the repo social preview; add topics. |
| All | `images/` | Curated screenshots, key art, banners, logo and icon. |

## 6. Timeline

Dates assume launch on **Thursday 2026-11-19**. Shift the whole table if the
ModDB page isn't approved in time. Post between 15:00 and 18:00 UTC, when
European evening and US daytime overlap; the QL community is EU-heavy.

### Phase 0 — Prep (Tue 17 – Wed 18 Nov)

- [ ] Create a permanent Discord invite and fill the `TODO` in `MESSAGING.md`
      and every post that references it.
- [ ] Confirm the project homepage URL (`darkmatter-quake.com` is the current
      repo setting) and whether it should carry an FnQL page.
- [ ] GitHub: upload `.github/social-preview.png` in Settings → Social preview;
      add topics `quake-live`, `quake`, `quake3`, `game-engine`, `sourceport`,
      `vulkan`, `idtech3`.
- [ ] Create the ModDB mod page from `moddb/PAGE.md`; upload gallery images
      from `moddb/MEDIA.md`; queue the launch article as a draft.
- [ ] Mirror the Windows MSVC and Linux packages on ModDB (`moddb/DOWNLOAD.md`)
      or link GitHub releases only. Mirrors give ModDB download stats.
- [ ] Read the posting rules of r/QuakeLive, r/quake and the Steam QL hub.
- [ ] Record the trailer (`social/VIDEO.md`) if it is ready; it is not a launch
      blocker.

### Phase 1 — Launch (Thu 19 – Sat 21 Nov)

- [ ] **Thu 19:** publish the ModDB article; post `reddit/r-QuakeLive.md`;
      post `steam/DISCUSSION.md`; send the Discord announcement; post the
      social thread. Reply to every comment for the first two hours.
- [ ] **Fri 20:** publish the Steam Guide (`steam/GUIDE.md`); collect the FAQ
      questions you didn't anticipate and add them to `MESSAGING.md`.
- [ ] **Sat 21:** weekend play: host a public FnQL server (FnQL-game, FFA or
      CA) and invite players from the threads. Screenshots from that session
      become the next gallery update.

### Phase 2 — Second wave (Mon 23 Nov – Sun 6 Dec)

- [ ] **Mon 23:** `reddit/r-quake.md` (lineage and renderers angle).
- [ ] **Wed 25:** server-operator post (`reddit/r-QuakeLive-servers.md`), also
      on the Steam hub.
- [ ] Send `press/OUTREACH.md` to Quake content creators and community
      newsletters, with the press kit.
- [ ] Publish the trailer on YouTube, then share it in each existing thread
      as a reply rather than as a new post.

### Phase 3 — Ongoing (every release)

- [ ] ModDB news post per release, built from the release notes'
      Highlights section (template in `moddb/ARTICLE-LAUNCH.md`).
- [ ] One **feature spotlight** every two weeks, rotating through:
      renderers and HDR; mouse input and CPI accuracy; HRTF audio; player
      highlighting; console quality-of-life; screenshots and video capture;
      server hardening; FnQL-game parity.
- [ ] A technical write-up for the developer audience (the lockstep parity
      harness is the strongest story). See §8 before posting it.
- [ ] Monthly metrics check against §3.

## 7. Engagement rules

- Be present: the first two hours after any Reddit or Steam post decide its
  reach. Answer questions with specifics and links.
- Don't ask for upvotes, and don't cross-post the same text everywhere.
  Each venue gets its own angle (the posts in this folder already differ).
- Turn every bug report in a thread into a GitHub issue and reply with the
  link. Visible follow-through is the best promotion a young project has.
- Stay out of comparisons that disparage other engines or communities.
  Credit Quake3e, ioquake3 and FnQ3; FnQL is built on them.
- When someone asks about bans, anti-cheat or "is this legal", answer with the
  wording in `MESSAGING.md` (Guardrails) and the FAQ in `press/FACT_SHEET.md`.

## 8. Risks and responses

| Risk | Response |
| --- | --- |
| Confusion over affiliation or trademarks | Trademark note on every long-form post; no QL logo in FnQL key art. |
| "Is this a cheat? Will I get banned?" | It is an engine replacement with Steam auth intact; server operators decide which clients they accept; FnQL never fakes authentication. Don't promise more. |
| Name read as crude ("Fappin'") | Lead with FnQL; spell the name out only where the venue tolerates it. |
| RTX expectations | Never headline ray tracing; call the RTX module experimental. |
| Platform disappointment (Linux/macOS client, 64-bit) | State plainly: Windows x86 client and Linux i686 dedicated server ship; other targets build from source for servers and development. |
| Closed-source Steam provider criticism | Be upfront: the engine is GPL-2.0; the Steam bridge is a separate compiled binary; FnQL runs without it in non-Steam mode. |
| Reverse-engineering legal questions in developer venues | Describe FnQL-game as rewritten from observed retail behavior with no copied code, link its rules, and don't post retail disassembly or addresses in promotional material. |
| Subreddit self-promotion rules | Read rules first; message moderators before posting if the rules ask for it. |
| Launch-day bug reports | Have the issue tracker and a known-issues list ready; ship a fix release quickly and say so in the threads. |

## 9. Measuring

Record weekly in this table:

| Week of | Stars | Release downloads | ModDB watchers | Reddit (votes/comments) | Notes |
| --- | --- | --- | --- | --- | --- |
| 2026-11-16 | 18 | 8 | – | – | baseline |
