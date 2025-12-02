#pragma once


#include <stdint.h>
#include <stddef.h>
#include <execinfo.h>
#include <cxxabi.h>
#include <dlfcn.h>
#include <vector>
#include <iostream>
#include <memory>


#ifdef EASYSPOT_DEBUG

    #define LOG(s) std::cout << "\n[" << __FILE__ << ":" << __LINE__ << "] " << s << "\n" << std::flush
    #define HERE LOG("HERE")
    #define DUMP(expr) std::cout << "\n" << #expr << "\n↳ " << (expr) << "\n"
    #define DEBUG_CODE(...) __VA_ARGS__
    #define ASSERTM(cond, msg) if (!(cond)) { LOG("Error: " << msg); panic(); }
    #define ASSERT(cond) if (!(cond)) { LOG("Failed Assert: `" << #cond << "`"); panic(); }

#else

    #define LOG(s) ;
    #define HERE ;
    #define DUMP(expr) ;
    #define DEBUG_CODE(...)
    #define ASSERTM(cond, msg) ;
    #define ASSERT(cond) ;
    
#endif


/// Compile with `-g to get symbol names`
void print_stacktrace()
{
    const int max_frames = 64;
    void* addrlist[max_frames];

    auto addrlen = backtrace(addrlist, max_frames);
    if (addrlen == 0)
    {
        std::cout << " ↳ <No stacktrace found, possibly corrupt>\n";
        return;
    }

    auto symbols = backtrace_symbols(addrlist, addrlen);
    auto idx_of_main = -1;

    // usual format to parse: "path(mangled_name+offset) [address]"
    for (auto i = 1; i < addrlen; i++)
    {
        std::string symbol = symbols[addrlen - i];
        size_t begin_name = 0;
        size_t end_name = 0;
        
        // searching for parenthesis to find the mangled name
        for (size_t j = 0; j < symbol.length(); j++)
        {
            if (symbol[j] == '(')
                begin_name = j;
            else if (symbol[j] == '+')
                end_name = j;
        }

        // printing spacing
        if (idx_of_main != -1)
            for (auto j = idx_of_main; j < i + 1; j++)
                std::cout << ' ';

        if (begin_name <= 0 || end_name <= begin_name)
        {
            if (idx_of_main != -1)
                std::cout << i - idx_of_main + 1 << " ↳ " << symbol << "\n";
            
            continue;
        }
        
        // skipping the open parenthesis
        begin_name++;
        auto mangled_name = symbol.substr(begin_name, end_name - begin_name);

        // we will skip all of the symbols that were called before main
        // then after main, we can start printing all the symbols
        if (mangled_name == "main")
        {
            idx_of_main = i;
            std::cout << " 1 ↳ <main>\n";
            continue;
        }
        
        if (idx_of_main == -1)
            continue;
        
        int status;
        auto demangled = abi::__cxa_demangle(mangled_name.c_str(), nullptr, nullptr, &status);

        std::cout << i - idx_of_main + 1 << " ↳ ";

        if (status == 0)
        {
            // demangled found
            std::cout << demangled;
            free(demangled);
        }
        else
        {
            // demangled not found
            std::cout << symbol;
        }

        std::cout << "\n";
    }

    std::cout << "\n" << std::flush;
    free(symbols);
}


void panic()
{
    print_stacktrace();
    std::abort();
}


using cstring = char const*;


/// A non-owning pointer (it has not clue about the size of the pointed block)
template<typename PointeeT>
struct ref
{
    PointeeT* bptr;

    ref(uint8_t* ptr)
    {
        bptr = (PointeeT*)ptr;
    }
    
    PointeeT& operator*()
    {
        return *bptr;
    }
    
    PointeeT* operator->()
    {
        return bptr;
    }
};


/// Untyped owning pointer
/// contains the actual pointer to the block and the size of the block
struct block
{
    uint8_t* bptr;

    block(size_t size)
    {
        bptr = new uint8_t[size];

        // storing the size of the block in the first bytes
        // and advancing the pointer by default
        ((size_t*)bptr)[0] = size;
        bptr += sizeof(size_t);
    }

    ~block()
    {
        // not allowed to deallocate internal block
    }

    size_t size()
    {
        return (bptr - sizeof(size_t))[0];
    }

    template<typename PointeeT>
    ref<PointeeT> as_ref()
    {
        return ref<PointeeT>(bptr);
    }
};


/// Typed owning pointer that holding a sequence of concrete elements
template<typename PointeeT>
struct seq
{
    block b;

    seq(size_t capacity) : b(capacity * sizeof(PointeeT))
    {

    }

    ~seq()
    {
        // not allowed to deallocate internal block
    }

    PointeeT& operator[](size_t idx)
    {
        return *nth(idx).bptr;
    }

    size_t capacity()
    {
        return b.size() / sizeof(PointeeT);
    }

    ref<PointeeT> nth(size_t idx)
    {
        ASSERTM(idx < capacity(), "Index out of bounds");
        return ref<PointeeT>(b.bptr + idx * sizeof(PointeeT));
    }
};
