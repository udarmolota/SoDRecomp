#include "stdafx.h"
#include "hash.h"

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <fstream>
#include <TinySHA1.hpp>
#endif

namespace sod
{
    namespace
    {
        std::string Hex(std::span<const uint8_t> digest)
        {
            std::string hex;
            for (const uint8_t b : digest)
                hex += fmt::format("{:02X}", b);
            return hex;
        }
    }

#ifdef _WIN32
    bool HashFile(HANDLE file, const wchar_t* algorithm, std::span<uint8_t> digest, uint64_t& size)
    {
        BCRYPT_ALG_HANDLE provider = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        bool ok = BCryptOpenAlgorithmProvider(&provider, algorithm, nullptr, 0) >= 0 &&
                  BCryptCreateHash(provider, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
        if (ok)
        {
            std::vector<uint8_t> buffer(1 << 20);
            size = 0;
            DWORD read = 0;
            while ((ok = ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr)) && read > 0)
            {
                size += read;
                if (BCryptHashData(hash, buffer.data(), read, 0) < 0)
                {
                    ok = false;
                    break;
                }
            }
            ok = ok && BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0) >= 0;
        }
        if (hash)
            BCryptDestroyHash(hash);
        if (provider)
            BCryptCloseAlgorithmProvider(provider, 0);
        return ok;
    }

    std::string FileSha1(const std::filesystem::path& path)
    {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return {};
        std::array<uint8_t, 20> digest;
        uint64_t size = 0;
        const bool ok = HashFile(file, BCRYPT_SHA1_ALGORITHM, digest, size);
        CloseHandle(file);
        return ok ? Hex(digest) : std::string();
    }

    std::string Sha1(std::span<const uint8_t> data)
    {
        std::array<uint8_t, 20> digest;
        // (BCryptHash takes a ULONG length.)
        if (data.size() > ULONG_MAX ||
            BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(data.data()), ULONG(data.size()),
                digest.data(), ULONG(digest.size())) < 0)
            return {};
        return Hex(digest);
    }

    bool Sha256(std::span<const uint8_t> data, std::span<uint8_t, 32> digest)
    {
        // (BCryptHash takes a ULONG length.)
        return data.size() <= ULONG_MAX &&
               BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(data.data()), ULONG(data.size()),
                   digest.data(), ULONG(digest.size())) >= 0;
    }
#else
    // POSIX: SHA-1 from XenonRecomp's TinySHA1, SHA-256 written out (FIPS 180-4).
    namespace
    {
        struct Sha256State
        {
            uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
            uint8_t block[64] = {};
            size_t blockUsed = 0;
            uint64_t total = 0;

            static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

            void Compress(const uint8_t* p)
            {
                static constexpr uint32_t k[64] = {
                    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
                uint32_t w[64];
                for (int i = 0; i < 16; i++)
                    w[i] = uint32_t(p[i * 4]) << 24 | uint32_t(p[i * 4 + 1]) << 16 | uint32_t(p[i * 4 + 2]) << 8 | p[i * 4 + 3];
                for (int i = 16; i < 64; i++)
                {
                    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }
                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
                for (int i = 0; i < 64; i++)
                {
                    const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
                    const uint32_t ch = (e & f) ^ (~e & g);
                    const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
                    const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
                    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    const uint32_t t2 = S0 + maj;
                    hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
                }
                h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
            }

            void Update(const uint8_t* data, size_t size)
            {
                total += size;
                while (size)
                {
                    const size_t take = std::min(size, sizeof(block) - blockUsed);
                    memcpy(block + blockUsed, data, take);
                    blockUsed += take;
                    data += take;
                    size -= take;
                    if (blockUsed == sizeof(block))
                    {
                        Compress(block);
                        blockUsed = 0;
                    }
                }
            }

            void Finish(uint8_t* digest)
            {
                const uint64_t bits = total * 8;
                const uint8_t one = 0x80;
                Update(&one, 1);
                const uint8_t zero = 0;
                while (blockUsed != 56)
                    Update(&zero, 1);
                uint8_t length[8];
                for (int i = 0; i < 8; i++)
                    length[i] = uint8_t(bits >> (56 - i * 8));
                Update(length, 8);
                for (int i = 0; i < 8; i++)
                {
                    digest[i * 4] = uint8_t(h[i] >> 24);
                    digest[i * 4 + 1] = uint8_t(h[i] >> 16);
                    digest[i * 4 + 2] = uint8_t(h[i] >> 8);
                    digest[i * 4 + 3] = uint8_t(h[i]);
                }
            }
        };
    }

    std::string FileSha1(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        sha1::SHA1 sha;
        std::vector<char> buffer(1 << 20);
        while (in.read(buffer.data(), std::streamsize(buffer.size())) || in.gcount() > 0)
            sha.processBytes(buffer.data(), size_t(in.gcount()));
        sha1::SHA1::digest8_t digest;
        sha.finalize(digest);
        return Hex(std::span<const uint8_t>(digest, 20));
    }

    std::string Sha1(std::span<const uint8_t> data)
    {
        sha1::SHA1 sha;
        sha.processBytes(data.data(), data.size());
        sha1::SHA1::digest8_t digest;
        sha.finalize(digest);
        return Hex(std::span<const uint8_t>(digest, 20));
    }

    bool Sha256(std::span<const uint8_t> data, std::span<uint8_t, 32> digest)
    {
        Sha256State state;
        state.Update(data.data(), data.size());
        state.Finish(digest.data());
        return true;
    }
#endif
}
