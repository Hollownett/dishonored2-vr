#define NOMINMAX
#include "../src/controller_glyph_atlas.hpp"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <iostream>
#include <vector>
std::vector<unsigned char> read(const char* path) {
    const std::filesystem::path source=__FILE__;
    const auto root=source.is_absolute()?source.parent_path().parent_path():std::filesystem::current_path();
    std::ifstream input(root/path,std::ios::binary);assert(input);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
int main() {
    using namespace d2vr;
    const char* paths[]{"artifacts/hud-re/ex/hud.iggy","artifacts/research/interaction-v221/quick_access_wheel.iggy","artifacts/research/interaction-v221/lib_wheel.iggy"};
    for(unsigned index=0;index<3;++index) {
        const auto& edit=native_art_movie_edits[index];const auto original=read(paths[index]);auto data=original;
        const std::string canonical=edit.path;
        auto backslash=canonical;std::replace(backslash.begin(),backslash.end(),'/','\\');
        auto upper=backslash;for(auto& c:upper)if(c>='a' && c<='z')c=static_cast<char>(c-('a'-'A'));
        for(const auto& accepted:{backslash,upper,std::string("native/prefix/")+canonical,std::string("C:\\native\\prefix\\")+backslash}) {
            auto matching=original;
            assert(patch_native_art_movie(accepted.c_str(),matching.data(),matching.size())==NativeArtMovieResult::patched);
        }
        const auto basename=canonical.substr(canonical.find_last_of('/')+1);
        for(const auto& rejected:{basename,std::string("other/")+basename,canonical+".old",std::string("not")+canonical,std::string(256,'x')+"/"+canonical}) {
            auto unchanged=original;
            assert(patch_native_art_movie(rejected.c_str(),unchanged.data(),unchanged.size())==NativeArtMovieResult::unsupported && unchanged==original);
        }
        std::array<char,256> unterminated{};unterminated.fill('x');
        assert(!native_art_movie_path(unterminated.data(),edit.path));
        assert(patch_native_art_movie("ui/unrelated_menu.iggy",data.data(),data.size())==NativeArtMovieResult::unsupported && data==original);
        assert(patch_native_art_movie(edit.path,data.data(),data.size()-1)==NativeArtMovieResult::rejected && data==original);
        data[100]^=1;const auto corrupted=data;
        assert(patch_native_art_movie(edit.path,data.data(),data.size())==NativeArtMovieResult::rejected && data==corrupted);data=original;
        assert(patch_native_art_movie(edit.path,data.data(),data.size())==NativeArtMovieResult::patched);
        auto expected=original; // every row for this movie, in table order (hud: crosshair, then button art)
        for(const auto& row:native_art_movie_edits)if(std::string(row.path)==edit.path)std::memcpy(expected.data()+row.offset,row.new_bytes.data(),row.count);
        assert(data==expected && data!=original);const auto patched=data;
        assert(patch_native_art_movie(edit.path,data.data(),data.size())==NativeArtMovieResult::already && data==patched);
        data[100]^=1;const auto changed_patched=data;
        assert(patch_native_art_movie(edit.path,data.data(),data.size())==NativeArtMovieResult::rejected && data==changed_patched);
    }
    std::cout<<"PASS: actual shipped native HUD/wheel movie SHA pins, chained rows (hud crosshair + button art), isolated same-size edits, unknown menu/mismatch refusal and idempotence\n";
}
