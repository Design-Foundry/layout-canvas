#include "vc/raster.hpp"
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>

namespace vc {
namespace {
constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr std::array<std::uint32_t,64> k={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
std::string sha256(std::vector<std::uint8_t> bytes) {
    const auto bits=std::uint64_t(bytes.size())*8;
    bytes.push_back(0x80); while(bytes.size()%64!=56)bytes.push_back(0);
    for(int i=7;i>=0;--i)bytes.push_back(std::uint8_t(bits>>(i*8)));
    std::array<std::uint32_t,8> h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(std::size_t offset=0;offset<bytes.size();offset+=64) {
        std::array<std::uint32_t,64>w{};
        for(int i=0;i<16;++i) for(int j=0;j<4;++j) w[i]=(w[i]<<8)|bytes[offset+4*i+j];
        for(int i=16;i<64;++i) {
            const auto a=w[i-15],b=w[i-2];
            w[i]=w[i-16]+(std::rotr(a,7)^std::rotr(a,18)^(a>>3))+w[i-7]+(std::rotr(b,17)^std::rotr(b,19)^(b>>10));
        }
        auto [a,b,c,d,e,f,g,v]=h;
        for(int i=0;i<64;++i) {
            const auto t=v+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
            const auto u=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            v=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=v;
    }
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:h)out<<std::setw(8)<<word;return out.str();
}
}
std::string rasterDigest(int width,int height,const std::vector<std::uint8_t>& pixels) {
    std::vector<std::uint8_t> bytes;
    for(int value:{width,height})for(int i=0;i<4;++i)bytes.push_back(std::uint8_t(std::uint32_t(value)>>(8*i)));
    bytes.insert(bytes.end(),pixels.begin(),pixels.end());return sha256(std::move(bytes));
}
std::string encodeRasterPixels(const std::vector<std::uint8_t>& bytes) {
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(std::size_t i=0;i<bytes.size();i+=3) {
        const auto n=(std::uint32_t(bytes[i])<<16)|(i+1<bytes.size()?std::uint32_t(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
        out.push_back(alphabet[(n>>18)&63]);out.push_back(alphabet[(n>>12)&63]);
        out.push_back(i+1<bytes.size()?alphabet[(n>>6)&63]:'=');out.push_back(i+2<bytes.size()?alphabet[n&63]:'=');
    }
    return out;
}
std::optional<std::vector<std::uint8_t>> decodeRaster(const Raster& image) {
    if(image.width<1||image.height<1||image.width>kRasterMaxSide||image.height>kRasterMaxSide||image.id.size()!=64)return std::nullopt;
    const auto expected=std::size_t(image.width)*image.height*4;
    if(image.pixels.size()!=(expected+2)/3*4)return std::nullopt;
    std::array<int,256> table;table.fill(-1);for(int i=0;i<64;++i)table[std::uint8_t(alphabet[i])]=i;
    std::vector<std::uint8_t> bytes;bytes.reserve(expected);
    for(std::size_t i=0;i<image.pixels.size();i+=4) {
        std::uint32_t n=0;
        for(int j=0;j<4;++j) {
            const auto ch=std::uint8_t(image.pixels[i+j]);
            if(ch=='=') {if(i+4!=image.pixels.size()||j<2)return std::nullopt;n<<=6;}
            else {if(table[ch]<0)return std::nullopt;n=(n<<6)|std::uint32_t(table[ch]);}
        }
        for(int j=0;j<3&&bytes.size()<expected;++j)bytes.push_back(std::uint8_t(n>>(16-8*j)));
    }
    if(bytes.size()!=expected||encodeRasterPixels(bytes)!=image.pixels||rasterDigest(image.width,image.height,bytes)!=image.id)return std::nullopt;
    return bytes;
}
std::vector<std::uint8_t> premultiplyRasterPixels(const std::vector<std::uint8_t>& pixels) {
    if(pixels.size()%4!=0) return {};
    auto upload=pixels;
    for(std::size_t i=0;i<upload.size();i+=4) for(std::size_t channel=0;channel<3;++channel)
        upload[i+channel]=std::uint8_t((unsigned(pixels[i+channel])*pixels[i+3]+127)/255);
    return upload;
}
bool validRaster(const Raster& image) {return decodeRaster(image).has_value();}
} // namespace vc
