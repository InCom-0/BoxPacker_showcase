#include <print>
#include <string_view>


int main(int argc, char *argv[]) {
    using namespace std::literals;

    std::print("{}", "Sample output"sv) ;
    return 0;
}