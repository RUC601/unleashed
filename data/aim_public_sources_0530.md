# Aim Public Data Sources 2026-05-30

This folder records the public data collected for the QA-driven aim architecture pass.

## Files

- `aim_public_weapon_projectiles_0530.tsv`: weapon/action rows aligned to current `WeaponSpec` rows. It records projectile speed, projectile radius, direct-hit window status, and headshot behavior when a public source exposed it.
- `aim_public_hero_geometry_0530.tsv`: public hero/bone hitbox findings and gaps. No inspected public source exposed a current, stable `hero -> bone -> radius` table, so unresolved rows are explicit instead of silently inventing values.

## Public Sources Checked

- Overwatch Wiki Projectile page: speed, projectile radius, shot type, headshot marker, and direct/splash damage notes for many weapon/ability projectiles.
  https://overwatch.fandom.com/wiki/Projectile
- Workshop.codes projectile-size page: projectile radius table, last updated 2026-02-27, with Workshop verification code.
  https://workshop.codes/wiki/articles/projectile-size-of-all-weapons-and-abilities
- Overwatch Wiki Critical Hit page: headshot/head-hitbox semantics and known exceptions.
  https://overwatch.fandom.com/wiki/Critical_hit
- `pluzorminuz/ow-hitbox`: public Workshop/Blender hitbox resources, fitted mesh/visualization notes, old patch comparisons, and ability-volume raw vertices.
  https://github.com/pluzorminuz/ow-hitbox
  https://raw.githubusercontent.com/pluzorminuz/ow-hitbox/master/hitbox_playground_vert-edge-list.txt
- Blizzard forum projectile radius measurement post: older radius table and tolerance context.
  https://us.forums.blizzard.com/en/overwatch/t/projectil-hitbox-size%E2%80%99s-radius-table-v20/618336

## Gaps Recorded

- Projectile size data is usable for most direct-hit windows, but Fandom and Workshop.codes disagree on a few values such as Genji, Echo, Baptiste alt fire, and Kiriko. Baptiste alt fire currently uses a temporary Workshop fallback in runtime code; conflicting rows remain marked for later verification.
- Public hero hitbox sources found during this pass are visual meshes, relative volume/surface metrics, or ability-volume vertex lists. They are not a current per-hero/per-bone radius dataset.
- `HeroGeometrySpec` therefore keeps conservative fallback radii with low confidence. Limb rows are extrapolated placeholders so auto-bone windows are not blank; the TSV records that these are not public measurements.
