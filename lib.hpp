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
    #define ASSERTM(cond, msg) if (!(cond)) { LOG("Error: " << msg); panic(); }
    #define ASSERT(cond) if (!(cond)) { LOG("Failed Assert: `" << #cond << "`"); panic(); }
    #define PANIC(msg) ASSERTM(false, msg)

#else

    #define LOG(s) ;
    #define HERE ;
    #define DUMP(expr) ;
    #define ASSERTM(cond, msg) ;
    #define ASSERT(cond) ;
    #define PANIC(msg) ;
    
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

/// Do not use this directly, represents a pointer that has its block size stored
/// in the sizeof(size_t) previous bytes of the pointer (ptr - sizeof(size_t))
using OwningPointer = uint8_t*;


// TODO: make this thread safe, remember different threads can use the same memory,
//       so i can't make this thread local but must be shared among all threads
#ifdef EASYSPOT_DEBUG
    struct RegistryRecord
    {
        OwningPointer block;
        uint16_t generation;
    };

    // TODO: make this actually performant and use data oriented design
    // TODO: implement generation logic + pointer flagging for local generation
    // TODO: implement last access tick to track elapsed time between last block access and block drop
    std::vector<RegistryRecord> debug_mem_registry;
#endif


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
        check_use();
        return *bptr;
    }
    
    PointeeT* operator->()
    {
        check_use();
        return bptr;
    }

    #ifdef EASYSPOT_DEBUG
        inline void check_use()
        {
            for (auto i = 0; i < debug_mem_registry.size(); i++)
            {
                auto record = debug_mem_registry[i];
                auto record_block_size = ((size_t*)(record.block - sizeof(size_t)))[0];
                if ((uint8_t*)bptr >= record.block && (uint8_t*)bptr <= record.block + record_block_size)
                    return;
            }

            PANIC("Use of dead reference");
        }
    #else
        inline void check_use()
        {

        }
    #endif
};


/// Untyped owning pointer
/// contains the actual pointer to the block and the size of the block
struct block
{
    OwningPointer bptr;

    block(size_t size)
    {
        // TODO: consider using uint32_t instead
        bptr = new uint8_t[sizeof(size_t) + size];

        // storing the size of the block in the first bytes
        // and advancing the pointer by default
        ((size_t*)bptr)[0] = size;
        bptr += sizeof(size_t);

        #ifdef EASYSPOT_DEBUG
            debug_mem_registry.push_back(RegistryRecord { .block = bptr, .generation = 0 });
        #endif
    }

    ~block()
    {
        // not allowed to deallocate internal block
    }

    void drop()
    {
        check_drop();
        auto actual_ptr = bptr - sizeof(size_t);
        delete[] actual_ptr;
    }

    #ifdef EASYSPOT_DEBUG
        inline void check_drop()
        {
            for (auto i = 0; i < debug_mem_registry.size(); i++)
                if (bptr == debug_mem_registry[i].block)
                {
                    std::swap(debug_mem_registry[i], debug_mem_registry.back());
                    debug_mem_registry.pop_back();
                    return;
                }
            
            PANIC("Drop of dead block. Maybe double drop?");
        }
    #else
        inline void check_drop()
        {
            
        }
    #endif

    size_t size()
    {
        return ((size_t*)(bptr - sizeof(size_t)))[0];
    }

    template<typename PointeeT>
    ref<PointeeT> as_ref()
    {
        return ref<PointeeT>(bptr);
    }
};


/// Typed owning pointer that holds a sequence of concrete elements
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

    void drop()
    {
        b.drop();
    }
};


/// Just like `block` has `ref`, so does `seq` with `slice`
struct slice
{
    uint8_t* bptr;

    // TODO: implement this
};
