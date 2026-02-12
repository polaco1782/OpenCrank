/*
 * Small dl helper utilities to centralize dlsym casting and string helpers
 */
#ifndef opencrank_CORE_DL_UTILS_HPP
#define opencrank_CORE_DL_UTILS_HPP

#include <dlfcn.h>
#include <string>
#include <algorithm>

namespace opencrank {

template<typename T>
inline T get_symbol(void* handle, const char* name) {
    dlerror(); // clear
    void* sym = dlsym(handle, name);
    const char* err = dlerror();
    if (err || !sym) return nullptr;
    return reinterpret_cast<T>(sym);
}

inline bool ends_with(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && 
           std::equal(suf.rbegin(), suf.rend(), s.rbegin());
}

} // namespace opencrank

#endif // opencrank_CORE_DL_UTILS_HPP
