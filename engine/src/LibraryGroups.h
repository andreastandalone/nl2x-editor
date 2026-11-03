#pragma once

// Shared by groupLibrary and groupEntries: the group of a nested folder, made when first met.

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

namespace nl2x {

// The group `folders` names under `top` (outermost first), adding the missing ones at the end of
// their parent's groups: fed in path order, the groups come out in path order.
template <typename Group>
Group& groupFor(Group& top, const std::vector<std::string>& folders)
{
    auto* group = &top;
    for (const auto& folder : folders) {
        auto found = std::find_if(group->groups.begin(), group->groups.end(),
                                  [&folder](const Group& each) { return each.name == folder; });
        if (found == group->groups.end()) {
            group->groups.push_back(Group { folder, {}, {} });
            found = std::prev(group->groups.end());
        }
        group = &*found;
    }
    return *group;
}

} // namespace nl2x
