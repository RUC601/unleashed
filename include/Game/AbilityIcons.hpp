#pragma once

#include <cctype>
#include <cstddef>
#include <string>

namespace OW {

struct HeroAbilityIcons {
    const char* heroSlug;
    const char* ability1Icon;
    const char* ability2Icon;
    const char* ultimateIcon;
};

inline constexpr HeroAbilityIcons kHeroAbilityIcons[] = {
    { "ana", "sleep-dart", "biotic-grenade", "nano-boost" },
    { "anran", "inferno-rush", "heat-shield", "vermillion-revival" },
    { "ashe", "coach-gun", "dynamite", "bob" },
    { "baptiste", "regenerative-burst", "immortality-field", "amplification-matrix" },
    { "bastion", "reconfigure", "a-36-tactical-grenade", "configuration-artillery" },
    { "brigitte", "whip-shot", "repair-pack", "rally" },
    { "cassidy", "combat-roll", "flashbang", "deadeye" },
    { "domina", "crystal-charge", "barrier-array", "panopticon" },
    { "doomfist", "seismic-slam", "rocket-punch", "meteor-strike" },
    { "dva", "boosters", "micro-missiles", "self-destruct" },
    { "echo", "flight", "sticky-bombs", "duplicate" },
    { "emre", "siphon-blaster", "cyber-frag", "override-protocol" },
    { "freja", "quick-dash", "updraft", "relentless-barrage" },
    { "genji", "swift-strike", "deflect", "dragonblade" },
    { "hanzo", "sonic-arrow", "storm-arrows", "dragonstrike" },
    { "hazard", "violent-leap", "jagged-wall", "downpour" },
    { "illari", "outburst", "healing-pylon", "captive-sun" },
    { "jetpack-cat", "jetpack", "headbutt", "ulterior-motive" },
    { "junker-queen", "commanding-shout", "carnage", "rampage" },
    { "junkrat", "concussion-mine", "steel-trap", "rip-tire" },
    { "juno", "glide-boost", "hyper-ring", "orbital-ray" },
    { "kiriko", "swift-step", "protection-suzu", "kitsune-rush" },
    { "lifeweaver", "rejuvenating-dash", "life-grip", "tree-of-life" },
    { "lucio", "soundwave", "amp-it-up", "sound-barrier" },
    { "mauga", "overrun", "cardiac-overdrive", "cage-fight" },
    { "mei", "cryo-freeze", "ice-wall", "blizzard" },
    { "mercy", "guardian-angel", "resurrect", "valkyrie" },
    { "mizuki", "quickstep", "remedy-aura", "kekkai-sanctuary" },
    { "moira", "fade", "biotic-orb", "coalescence" },
    { "orisa", "fortify", "javelin-spin", "terra-surge" },
    { "pharah", "jump-jet", "concussive-blast", "barrage" },
    { "ramattra", "nemesis-form", "ravenous-vortex", "annihilation" },
    { "reaper", "wraith-form", "shadow-step", "death-blossom" },
    { "reinhardt", "charge", "fire-strike", "earthshatter" },
    { "roadhog", "take-a-breather", "pig-pen", "whole-hog" },
    { "sierra", "anchor-drone", "tremor-charge", "trailblazer" },
    { "sigma", "kinetic-grasp", "accretion", "gravitic-flux" },
    { "sojourn", "power-slide", "disruptor-shot", "overclock" },
    { "soldier-76", "helix-rockets", "biotic-field", "tactical-visor" },
    { "sombra", "translocator", "virus", "emp" },
    { "symmetra", "teleporter", "sentry-turret", "photon-barrier" },
    { "torbjorn", "overload", "deploy-turret", "molten-core" },
    { "tracer", "blink", "recall", "pulse-bomb" },
    { "vendetta", "whirlwind-dash", "warding-stance", "sundering-blade" },
    { "venture", "drill-dash", "burrow", "tectonic-shock" },
    { "widowmaker", "grappling-hook", "venom-mine", "infra-sight" },
    { "winston", "jump-pack", "barrier-projector", "primal-rage" },
    { "wrecking-ball", "grappling-claw", "adaptive-shield", "minefield" },
    { "wuyang", "guardian-wave", "rushing-torrent", "tidal-blast" },
    { "zarya", "particle-barrier", "projected-barrier", "graviton-surge" },
    { "zenyatta", "orb-of-discord", "orb-of-harmony", "transcendence" },
};

inline constexpr const auto& AllHeroAbilityIcons() {
    return kHeroAbilityIcons;
}

inline constexpr size_t HeroAbilityIconCount() {
    return sizeof(kHeroAbilityIcons) / sizeof(kHeroAbilityIcons[0]);
}

std::string HeroDisplayNameToSlug(const std::string& displayName);

inline const HeroAbilityIcons* GetAbilityIcons(const std::string& slugOrDisplayName) {
    const std::string slug = HeroDisplayNameToSlug(slugOrDisplayName);
    if (slug.empty())
        return nullptr;

    for (const HeroAbilityIcons& icons : kHeroAbilityIcons) {
        if (slug == icons.heroSlug)
            return &icons;
    }
    return nullptr;
}

} // namespace OW
