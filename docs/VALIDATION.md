# Public preview validation

This release carries the completed local gameplay profile into the first public download. It is not a promise that every possible save, mod combination or game runtime has been exercised.

| Check | Recorded result |
|---|---|
| Native core desktop suite, final profile and portable source copy |3446 passed,0 failed|
| Scan desktop suite |350 passed,0 failed|
| Protection scenario matrix, chosen combat+follow profile |380 passed,0 failed|
| Actual-engine previously uncached attacker, isolated proof fixture |20 passed,0 failed|
| Actual-engine continuous follow, stops, reversal and combat handoff |23 passed,0 failed|
| Actual-engine final load case |11 passed,0 failed|
| Canonical runtime Papyrus |9 scripts, strict compilation and zero assembly clobbers|
| ESP source reproduction |12514 bytes,44 records, SHA25663A44DB9068434807184F692587EB19CF4FC55F157623D0CD886A549249909C2;101/0 record/VMAD checks|

The attacker-isolation fixture showed real hit → admission → target → native kill, with no friendly target/hit. Its test-only scan isolation differs from normal release settings, so its observed timings are not a universal latency guarantee.

Continuous-follow validation observed stopping convergence, reversal, refollow and transition into a real native kill. Speed peaks were sampled. The25% follow and5% combat figures describe changed configuration limits and acceleration, not measured trip-time percentages. Indoor caps, braking, stopping, doorway and protection constraints remain.

Load/follow/wait/off lifecycle tests used isolated test saves. Personal saves were not used as gameplay test fixtures. The local journaled installer reported saves unchanged, and the installed local payload was hash-verified. The public archive omits all test ESPs/helpers/startup overrides and dependencies belonging to other authors.

The public distribution removes local PDB/source-path metadata from the DLL and PEX headers/descriptive metadata. The metadata sanitizer's receipt verifies unchanged executable PE sections and unchanged Papyrus executable tails. The original local build and rollback remain separate.

Known scope limits: experimental extra prop pushing and capsule pass remain off; other game executable versions/VR/consoles are unsupported; not every mod load order or mid-playthrough removal has been tested. No uniform FPS improvement or universal performance-cost bound is claimed. Removal preparation cannot promise that an already-used save becomes equivalent to a pre-install save.
