// Registers a custom rcheevos filereader that transparently decompresses RVZ/WIA disc images,
// so rc_hash_generate_from_file sees a plain ISO. Call this for .rvz/.wia inputs before hashing.
// Implemented in HashRVZ.cpp.
#pragma once

void rc_hash_init_rvz_filereader();

// Identifies an RVZ/WIA disc image by reading its header's disc_type field.
// Returns RC_CONSOLE_GAMECUBE or RC_CONSOLE_WII, or 0 if the file is not a valid
// RVZ/WIA or the disc type is unknown. Lets "?" auto-detection map .rvz/.wia correctly.
int rc_hash_rvz_detect_console(const char* path);
