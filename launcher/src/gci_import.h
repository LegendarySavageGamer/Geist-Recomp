#pragma once
#include <string>
// Copies a Dolphin/GameCube .gci save into a raw memory card image (memcard_a.raw), replacing
// a save of the same game and name if the card has one. The card is backed up beside itself
// first (<card>.before-import.raw). Empty error = success.
std::string gci_import(const std::string& gci_path, const std::string& card_path);
