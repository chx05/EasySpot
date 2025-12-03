#include "../lib.hpp"

int main()
{
    auto b = block(16);
    DUMP(b.size());

    auto s = seq<int32_t>(10);
    s[0] = 123;
    s[1] = 456;
    DUMP(s.capacity());
    DUMP(s[0]);
    DUMP(s[1]);

    auto r = b.as_ref<uint64_t>();
    *r = 789;
    DUMP(*r);

    auto n = s.nth(0);
    DUMP(*n);
    *n = 111;
    DUMP(*n);

    HERE;
    LOG("s cap = " << s.capacity());

    // ERRORS

    //auto out_of_bounds_ref = s.nth(s.capacity());
    //auto out_of_bounds = s[s.capacity()];

    //b.drop(); *r = 2;
    //b.drop(); b.drop();

    //s.drop(); *n = 0;

    // TODO, print them all (mainly size), print number of undropped blocks
    //check_registry_for_undropped_blocks();
    return 0;
}
