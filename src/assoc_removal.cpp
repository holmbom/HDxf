#include "AssocExtended.hpp"
#include <unordered_map>

// What AutoCAD removes with the geometry an associative action stands on (phase 9, step 9.15; plan
// 2026-10-27-fas-9-typer.md, F9.12). AutoCAD 2025's own erases (doc/facit/fas9_assoc_core.scr's profile_erased and
// fas9_assoc_extended.scr's trim_curve_erased, offset_source_erased, patch_curve_erased, network_curve_erased,
// solid_erased, fillet_wall_erased, constrained_line_erased, in 2000 … 2018 and both formats):
//   - erasing the profile of surfaces (a line two extrusions share, a patch's curve, a network's curve, a cutting circle, the
//     source of an offset, the wall a fillet joined, the solid dimensions stand on) removes every action that depended on it
//     with what the action owns: its parameters, its dependencies and its body. The network's list of actions and the
//     dependency chains lose them, the surfaces and curves that are left have their reactors cut
//   - an action that restores an entity's state (SURFTRIM, SURFFILLET and the edge operations make one before they change a
//     surface) goes with the action that changed the entity: its dependency's successor in the chain is gone
//   - a 2D constraint group is not removed: the nodes that stood on the erased line go (assoc_constraints.cpp), and with a
//     dimensional constraint's two dependencies and its variable (the user's own parameters stay)
// "Gone" is a handle with no record any more: in a drawing read from a file, one the program removed.

namespace hdxf {
namespace internal {

namespace {

const AssocDependencyBase* dependency_of(const Object& o) {
    switch (o.type) {
        case ObjectType::AssocDependency: return &o.as_assoc_dependency().dependency;
        case ObjectType::AssocGeomDependency: return &o.as_assoc_geom_dependency().dependency;
        case ObjectType::AssocValueDependency: return &o.as_assoc_value_dependency().dependency;
        default: return nullptr;
    }
}

uint64_t value_of(const std::string& handle) {
    uint64_t v = 0;
    for (char c : handle) {
        int d = 0;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return 0;
        v = v * 16 + static_cast<uint64_t>(d);
    }
    return v;
}

} // namespace

std::vector<uint64_t> assoc_orphaned_actions(const Drawing& dwg, const std::function<bool(const std::string&)>& live,
                                             const std::unordered_set<uint64_t>& dropped) {
    std::unordered_map<uint64_t, const Object*> by_handle;
    for (const Object& o : dwg.objects()) by_handle[value_of(o.handle)] = &o;
    std::unordered_set<uint64_t> gone;          // what goes now (not yet in `dropped`)
    auto is_gone = [&](const std::string& h) {
        if (h.empty() || h == "0") return false;
        uint64_t v = value_of(h);
        return gone.count(v) > 0 || dropped.count(v) > 0 || !live(h);
    };
    std::vector<uint64_t> order;
    auto add = [&](const std::string& h) {
        uint64_t v = value_of(h);
        if (v == 0 || dropped.count(v) || !gone.insert(v).second) return false;
        order.push_back(v);
        return true;
    };

    bool more = true;
    while (more) {
        more = false;
        for (const Object& o : dwg.objects()) {
            if (o.type == ObjectType::AssocAction) {
                if (is_gone(o.handle)) continue;
                const AssocActionBase& a = o.as_assoc_action().action;
                bool orphaned = false;
                for (const std::string& dh : a.dependencies) {
                    auto it = by_handle.find(value_of(dh));
                    if (it == by_handle.end() || is_gone(dh)) continue;
                    const AssocDependencyBase* dep = dependency_of(*it->second);
                    if (!dep) continue;
                    // the object the dependency stands on is gone
                    if (is_gone(dep->dependent_on_object)) orphaned = true;
                }
                // a restore-entity-state action goes with the action that changed what it restores: the next dependency of its chain
                auto body = by_handle.find(value_of(a.action_body));
                if (!orphaned && body != by_handle.end() && body->second->type == ObjectType::AssocRestoreEntityStateActionBody) {
                    for (const std::string& dh : a.dependencies) {
                        auto it = by_handle.find(value_of(dh));
                        const AssocDependencyBase* dep = it == by_handle.end() ? nullptr : dependency_of(*it->second);
                        if (!dep || dep->next.empty() || dep->next == "0") continue;
                        auto next = by_handle.find(value_of(dep->next));
                        if (next == by_handle.end() || is_gone(dep->next) || is_gone(next->second->owner)) orphaned = true;
                    }
                }
                if (orphaned && add(o.handle)) more = true;
            } else if (o.type == ObjectType::Assoc2dConstraintGroup) {
                if (is_gone(o.handle)) continue;
                // the dependencies the group owns on geometry that is gone go (AutoCAD's erase of a constrained line)
                for (const std::string& dh : o.as_assoc_2d_constraint_group().dependencies) {
                    auto it = by_handle.find(value_of(dh));
                    if (it == by_handle.end() || is_gone(dh)) continue;
                    const AssocDependencyBase* dep = dependency_of(*it->second);
                    if (dep && it->second->type == ObjectType::AssocGeomDependency && is_gone(dep->dependent_on_object) && add(dh)) more = true;
                }
                // the dimensional constraints that go take their two dependencies (the value dependency of the variable, the
                // dependency on the dimension) and the variable the first one stands on
                for (const auto& [node, dep] : assoc_group_removed_nodes(o.as_assoc_2d_constraint_group(), [&](const std::string& h) {
                         return !is_gone(h);
                     })) {
                    (void)node;
                    if (dep.empty() || dep == "0" || is_gone(dep)) continue;
                    if (add(dep)) more = true;
                    auto it = by_handle.find(value_of(dep));
                    const AssocDependencyBase* d = it == by_handle.end() ? nullptr : dependency_of(*it->second);
                    if (d && it->second->type == ObjectType::AssocValueDependency && !d->dependent_on_object.empty() &&
                        d->dependent_on_object != "0") {
                        auto target = by_handle.find(value_of(d->dependent_on_object));
                        if (target != by_handle.end() && target->second->type == ObjectType::AssocVariable && add(d->dependent_on_object)) {
                            more = true;
                        }
                    }
                }
            }
        }
    }
    return order;
}

} // namespace internal
} // namespace hdxf
