#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/controller_glyph_atlas.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}

int main() {
    try {
        using namespace d2vr;
        // Table sanity: 30 unique names, 15 normal plus 15 _s, block-aligned sizes inside the pack.
        require(sizeof(kXboxGlyphs)/sizeof(kXboxGlyphs[0])==30 && sizeof(kXboxGlyphRects)/sizeof(kXboxGlyphRects[0])==30,"Xbox glyph table is not 30 entries");
        unsigned small=0,normal=0;
        for(unsigned i=0;i<30;++i) {
            const GlyphTexture& glyph=kXboxGlyphs[i];const GlyphRect& rect=kXboxGlyphRects[i];
            require(std::strcmp(glyph.name,rect.name)==0,"Glyph and rect tables are out of order");
            require(glyph.label && *glyph.label,"Glyph has no Quest label");
            require(glyph.width && glyph.height && rect.x==0 && rect.y==0,"Glyph rect is malformed");
            require(rect.x+rect.w<=glyph.width && rect.y+rect.h<=glyph.height,"Glyph rect exceeds its texture");
            require(rect.w==glyph.width && rect.h==glyph.height,"Glyph rect does not cover its texture");
            require(glyph.bytes==((glyph.width+3)/4)*((glyph.height+3)/4)*kGlyphBlockBytes,"Glyph byte size does not match its block count");
            require(glyph.offset+glyph.bytes<=kGlyphPackBytes && glyph.signature,"Glyph lies outside the pack");
            std::strstr(glyph.name,"_XBOX_s")?++small:++normal;
            for(unsigned j=i+1;j<30;++j) {
                require(std::strcmp(glyph.name,kXboxGlyphs[j].name)!=0,"Duplicate glyph name");
                require(glyph.signature!=kXboxGlyphs[j].signature,"Duplicate glyph signature");
                require(glyph.offset!=kXboxGlyphs[j].offset,"Duplicate glyph offset");
            }
        }
        require(small==15 && normal==15,"Xbox glyph table is not 15 normal plus 15 small");
        // Non-overlapping ascending pack layout.
        for(unsigned i=0;i<30;++i) for(unsigned j=0;j<30;++j) if(i!=j) {
            const auto& a=kXboxGlyphs[i],&b=kXboxGlyphs[j];
            require(a.offset+a.bytes<=b.offset || b.offset+b.bytes<=a.offset,"Glyph slices overlap");
        }
        // Signature: cross-checked against the Python generator, deterministic and sensitive.
        std::vector<std::uint8_t> synthetic(1296);
        for(std::size_t i=0;i<synthetic.size();++i) synthetic[i]=static_cast<std::uint8_t>(i*31);
        require(atlas_signature(synthetic.data(),synthetic.size())==0x5a83fa2c57a1835eull,"Small signature does not match the generator");
        std::vector<std::uint8_t> large(40000);
        for(std::size_t i=0;i<large.size();++i) large[i]=static_cast<std::uint8_t>(i*131+7);
        require(atlas_signature(large.data(),large.size())==0x64f7e8c9b05e9a85ull,"Sampled signature does not match the generator");
        std::vector<std::uint8_t> tiny(100);
        for(std::size_t i=0;i<tiny.size();++i) tiny[i]=static_cast<std::uint8_t>(i*7);
        require(atlas_signature(tiny.data(),tiny.size())==0x96ad9706ab2fabc9ull,"Tiny signature does not match the generator");
        require(atlas_signature(synthetic.data(),synthetic.size())==atlas_signature(synthetic.data(),synthetic.size()),"Signature is not deterministic");
        require(atlas_signature(nullptr,0) && atlas_signature(nullptr,0)!=atlas_signature(tiny.data(),tiny.size()),"Empty signature is not distinct");
        synthetic[700]^=0x40;
        require(atlas_signature(synthetic.data(),synthetic.size())!=0x5a83fa2c57a1835eull,"Signature ignores byte flips");
        require(kGlyphPackSignature && kGlyphPackSignature!=atlas_signature(synthetic.data(),synthetic.size()),"Pack signature is not distinct");
        // Runtime matcher: exact dims plus signature, and rejects wrong dims, sizes and data.
        const std::uint8_t* probe=large.data();
        require(find_xbox_glyph(36,36,kXboxGlyphs[17].signature)==&kXboxGlyphs[17],"Signature lookup missed a glyph");
        require(find_xbox_glyph(36,36,probe,1296)==nullptr,"Lookup accepted the wrong texture data");
        require(find_xbox_glyph(37,36,kXboxGlyphs[17].signature)==nullptr,"Lookup ignored the width");
        require(find_xbox_glyph(36,37,kXboxGlyphs[17].signature)==nullptr,"Lookup ignored the height");
        require(find_xbox_glyph(36,36,probe,0)==nullptr && find_xbox_glyph(36,36,nullptr,1296)==nullptr,"Lookup accepted empty data");
        // Paint: only the requested rect changes, and the label paints white text on a dark card.
        const unsigned cw=128,ch=96;
        std::vector<std::uint8_t> canvas(static_cast<std::size_t>(cw)*ch*4,0x5a),before=canvas;
        const GlyphRect rect{"A_XBOX",34,18,60,60};
        paint_quest_glyphs(canvas.data(),cw,ch,rect,kXboxGlyphs[3].label);
        unsigned inside=0,text=0,outside_changed=0,transparent=0;
        for(unsigned y=0;y<ch;++y) for(unsigned x=0;x<cw;++x) {
            const auto* after=&canvas[(static_cast<std::size_t>(y)*cw+x)*4],*was=&before[(static_cast<std::size_t>(y)*cw+x)*4];
            if(x>=rect.x && x<rect.x+rect.w && y>=rect.y && y<rect.y+rect.h) {
                if(std::memcmp(after,was,4)!=0) ++inside;
                if(after[0]>200 && after[1]>200 && after[2]>200 && after[3]==255) ++text;
                if(!after[3]) ++transparent;
            } else if(std::memcmp(after,was,4)!=0) ++outside_changed;
        }
        require(inside>500 && text>40,"Quest label did not paint inside the rect");
        require(transparent>0,"Quest label card has no transparent corners");
        require(outside_changed==0,"Quest label painted outside its rect");
        require(canvas.size()==static_cast<std::size_t>(cw)*ch*4,"Quest label wrote past the bitmap");
        std::vector<std::uint8_t> tile(36*36*4,0),tile_again;
        paint_quest_glyphs(tile.data(),36,36,kXboxGlyphs[26].label);
        tile_again=tile;
        require(tile!=std::vector<std::uint8_t>(36*36*4,0),"Whole-image label did not paint");
        paint_quest_glyphs(tile.data(),36,36,kXboxGlyphs[26].label);
        require(tile==tile_again,"Whole-image label is not deterministic");
        // Every label must render white text inside its own glyph texture, with clear corners.
        const unsigned cell_w=128,cell_h=120,row_count=4;
        std::vector<std::uint8_t> preview(static_cast<std::size_t>(cell_w)*8*cell_h*row_count*4,0x40);
        for(unsigned i=0;i<30;++i) {
            const auto& glyph=kXboxGlyphs[i];
            std::vector<std::uint8_t> image(static_cast<std::size_t>(glyph.width)*glyph.height*4,0);
            paint_quest_glyphs(image.data(),glyph.width,glyph.height,glyph.label);
            require(!image[3] && !image[image.size()-4+3],"Glyph label has no transparent corner");
            unsigned white=0;
            for(std::size_t pixel=0;pixel<image.size();pixel+=4)
                if(image[pixel]>200 && image[pixel+1]>200 && image[pixel+2]>200 && image[pixel+3]==255) ++white;
            require(white>20,"Glyph label text is missing or too small");
            const unsigned column=i%8,row=i/8,left=column*cell_w+(cell_w-glyph.width)/2,top=row*cell_h+(cell_h-glyph.height)/2;
            for(unsigned y=0;y<glyph.height;++y) for(unsigned x=0;x<glyph.width;++x)
                std::memcpy(&preview[(static_cast<std::size_t>(top+y)*cell_w*8+left+x)*4],&image[(static_cast<std::size_t>(y)*glyph.width+x)*4],4);
        }
        std::ofstream export_preview("artifacts/research/glyphs/controller-glyph-preview.rgba",std::ios::binary);
        export_preview.write(reinterpret_cast<const char*>(preview.data()),static_cast<std::streamsize>(preview.size()));
        require(static_cast<bool>(export_preview),"Cannot save the controller glyph preview");
        std::cout<<"PASS: 30 _XBOX glyph textures (15 normal + 15 _s) with unique names, signatures and non-overlapping pack slices; FNV-1a sampling matches the generator and rejects wrong dims/data; GDI Quest labels paint inside their rect only, with transparent corners, legible white text at every glyph size and deterministic output\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
