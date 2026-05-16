#include <print>
#include <string_view>


int main(int argc, char *argv[]) {
    using namespace std::literals;

    std::println("{}", "Sample output"sv) ;
    return 0;
}