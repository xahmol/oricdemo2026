// strings_en.h - English UI strings
// Only the messages actually referenced by the copied include/ library
// (include/loci.c) live here. Add app-specific strings as the demo grows.

#ifndef STRINGS_EN_H
#define STRINGS_EN_H

#define MSG_LOCI_NOT_FOUND  "No LOCI device detected or firmware too old."
#define MSG_PRESS_KEY_EXIT  "Press any key to exit."

// src/section_credits.c's own closing-credits scroll lines. Individual
// #defines rather than a shared array here, so this file stays a plain
// gateway of string literals (no C array data gets compiled into every
// translation unit that includes strings.h, e.g. include/loci.c -- see
// section_credits.c's own credit_lines[] for the array built from these).
#define MSG_CREDIT_TITLE    "ORICDEMO2026...."
#define MSG_CREDIT_HOMAGE   "a homage to \"Welcome to Oric Atmos\" (1985)...."
#define MSG_CREDIT_AUTHOR   "coded by Xander Mol (xahmol)...."
#define MSG_CREDIT_REPO     "github.com/xahmol/oricdemo2026...."
#define MSG_CREDIT_TOOLS    "built with Oscar64 + Claude Code, 2026...."
#define MSG_CREDIT_BRAND    "presented by idi8b...."
#define MSG_CREDIT_MUSIC1   "music: Mr.Lou - Dewfall Productions - Stepping Out (2019)...."
#define MSG_CREDIT_MUSIC2   "music: Tom & Jerry - Boules Et Bits...."
#define MSG_CREDIT_BIRD     "bird sprite: mihai-dragan / oric_BAS (MIT)...."
#define MSG_CREDIT_LOGO     "Oric Atmos logo: Oric International / Wikimedia Commons...."
#define MSG_CREDIT_SUNSET   "sunset photo: Artem Beliaikin, Wikimedia Commons, CC0...."
#define MSG_CREDIT_THANKS   "thanks for watching!...."

#endif // STRINGS_EN_H
