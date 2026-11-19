#pragma once
#include "ProTrackerData.h"

// Parses public guide facts from the source page, never website application code.
// ExpectedSlug is the exact source hero slug used by the caller's request.
ProTrackerData::Snapshot DotacoachParse(const std::string& Html, const std::string& ExpectedSlug,
    unsigned Hero, unsigned Role, const ProTrackerData::Names& Items, const ProTrackerData::Names& Abilities);
