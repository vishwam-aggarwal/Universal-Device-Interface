#pragma once

// ==================================================================
// UdiLinkHook -- what happens THE MOMENT an attribute is written: every
// hook on its chain is called, in the writer's thread, right after the
// value is stored.
//
// A framework's link uses it: a motor's io OUT written by its update()
// reaches the io server's Term at once, and the Term's own hook (its
// server's) sends it on. Nothing waits for the end of a period. An io
// server whose attributes are not UdiAttr (built from a definition file)
// fires its chain itself after it writes a value (udiFireLinks).
//
// The hooks are nodes the framework owns (no heap): it adds one to an
// attribute's chain through Attr::links when it resolves a link, and
// takes it out again when it stops. A hook only copies or forwards a
// value; it never blocks. Values are written under the framework's tree
// lock, so the hooks run under it too.
// ==================================================================

struct UdiLinkHook {
    void (*fn)(void* ctx);
    void* ctx;
    UdiLinkHook* next;
};

// Calls every hook of a chain (nullptr: none).
inline void udiFireLinks(UdiLinkHook* head) {
    while (head != nullptr) {
        UdiLinkHook* next = head->next;   // a hook may take itself out
        head->fn(head->ctx);
        head = next;
    }
}

// Adds a hook at the end of a chain.
inline void udiLinkAdd(UdiLinkHook** chain, UdiLinkHook* hook) {
    hook->next = nullptr;
    while (*chain != nullptr) chain = &(*chain)->next;
    *chain = hook;
}

// Takes a hook out of a chain (nothing when it is not on it).
inline void udiLinkRemove(UdiLinkHook** chain, UdiLinkHook* hook) {
    while (*chain != nullptr) {
        if (*chain == hook) { *chain = hook->next; hook->next = nullptr; return; }
        chain = &(*chain)->next;
    }
}
