// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptProtection.h"
#include "ApplicationInternal.h"

#include <limits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace VeroScript::Application
{
	namespace
	{
		class FSodium final : public ICryptoProvider
		{
		public:
			std::string GetIdentity() const override
			{
				return Identity;
			}
			bool Random(std::vector<std::uint8_t>& Bytes, std::string&) const override
			{
				RandomBytes(Bytes.data(), Bytes.size());
				return true;
			}
			bool Encrypt(const std::vector<std::uint8_t>& Plain, const std::vector<std::uint8_t>& Aad,
				const std::vector<std::uint8_t>& Nonce, const std::vector<std::uint8_t>& Key,
				std::vector<std::uint8_t>& Out, std::string& Error) const override
			{
				if (Key.size() != 32 || Nonce.size() != 24 || Plain.size() > std::numeric_limits<std::size_t>::max() - 16)
				{
					Error = "invalid XChaCha20-Poly1305 key, nonce or message size";
					return false;
				}
				Out.resize(Plain.size() + 16);
				unsigned long long Length = 0;
				if (EncryptAead(Out.data(), &Length, Plain.data(), Plain.size(), Aad.data(), Aad.size(),
					nullptr, Nonce.data(), Key.data()) != 0 || Length != Out.size())
				{
					Out.clear();
					Error = "XChaCha20-Poly1305 encryption failed";
					return false;
				}
				return true;
			}
			bool Decrypt(const std::vector<std::uint8_t>& Cipher, const std::vector<std::uint8_t>& Aad,
				const std::vector<std::uint8_t>& Nonce, const std::vector<std::uint8_t>& Key,
				std::vector<std::uint8_t>& Out, std::string& Error) const override
			{
				if (Key.size() != 32 || Nonce.size() != 24 || Cipher.size() < 16)
				{
					Error = "invalid XChaCha20-Poly1305 key, nonce or ciphertext size";
					return false;
				}
				Out.resize(Cipher.size() - 16);
				unsigned long long Length = 0;
				if (DecryptAead(Out.data(), &Length, nullptr, Cipher.data(), Cipher.size(), Aad.data(), Aad.size(),
					Nonce.data(), Key.data()) != 0 || Length != Out.size())
				{
					if (!Out.empty())
					{
						Zero(Out.data(), Out.size());
					}
					Out.clear();
					Error = "protected artifact authentication failed";
					return false;
				}
				return true;
			}
			bool Sign(const std::vector<std::uint8_t>& Message, const std::vector<std::uint8_t>& Key,
				std::vector<std::uint8_t>& Signature, std::string& Error) const override
			{
				if (Key.size() != 64)
				{
					Error = "Ed25519 requires a 64-byte secret key";
					return false;
				}
				Signature.resize(64);
				unsigned long long Length = 0;
				if (SignDetached(Signature.data(), &Length, Message.data(), Message.size(), Key.data()) != 0 || Length != 64)
				{
					Signature.clear();
					Error = "Ed25519 signing failed";
					return false;
				}
				return true;
			}
			bool Verify(const std::vector<std::uint8_t>& Message, const std::vector<std::uint8_t>& Key,
				const std::vector<std::uint8_t>& Signature, std::string& Error) const override
			{
				if (Key.size() != 32 || Signature.size() != 64 ||
					VerifyDetached(Signature.data(), Message.data(), Message.size(), Key.data()) != 0)
				{
					Error = "Ed25519 signature verification failed";
					return false;
				}
				return true;
			}
			explicit FSodium(const std::string& Path)
			{
				#if defined(_WIN32)
				Module = LoadLibraryExW(PathUtil::FromUtf8(Path).c_str(), nullptr,
					LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
				#else
				Module = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
				#endif
				if (!Module)
				{
					Detail::Fail("crypto-load", "cannot load the explicit libsodium implementation", Path);
				}
			}
			~FSodium() override
			{
				#if defined(_WIN32)
				FreeLibrary(static_cast<HMODULE>(Module));
				#else
				dlclose(Module);
				#endif
			}
			void Initialize(std::string Hash)
			{
				RandomBytes = Symbol<decltype(RandomBytes)>("randombytes_buf");
				EncryptAead = Symbol<decltype(EncryptAead)>("crypto_aead_xchacha20poly1305_ietf_encrypt");
				DecryptAead = Symbol<decltype(DecryptAead)>("crypto_aead_xchacha20poly1305_ietf_decrypt");
				SignDetached = Symbol<decltype(SignDetached)>("crypto_sign_detached");
				VerifyDetached = Symbol<decltype(VerifyDetached)>("crypto_sign_verify_detached");
				Zero = Symbol<decltype(Zero)>("sodium_memzero");
				const auto Init = Symbol<int(*)()>("sodium_init");
				if (Init() < 0)
				{
					Detail::Fail("crypto-init", "libsodium could not initialize");
				}
				Identity = "libsodium:" + std::move(Hash);
			}
		private:
			template<class T> T Symbol(const char* Name)
			{
				#if defined(_WIN32)
				const auto Address = GetProcAddress(static_cast<HMODULE>(Module), Name);
				#else
				const auto Address = dlsym(Module, Name);
				#endif
				if (!Address)
				{
					Detail::Fail("crypto-symbol", "libsodium is missing a required symbol", Name);
				}
				return reinterpret_cast<T>(Address);
			}
			void* Module = nullptr;
			std::string Identity;
			void (*RandomBytes)(void*, std::size_t) = nullptr;
			int (*EncryptAead)(unsigned char*, unsigned long long*, const unsigned char*, unsigned long long,
				const unsigned char*, unsigned long long, const unsigned char*, const unsigned char*, const unsigned char*) = nullptr;
			int (*DecryptAead)(unsigned char*, unsigned long long*, unsigned char*, const unsigned char*, unsigned long long,
				const unsigned char*, unsigned long long, const unsigned char*, const unsigned char*) = nullptr;
			int (*SignDetached)(unsigned char*, unsigned long long*, const unsigned char*, unsigned long long, const unsigned char*) = nullptr;
			int (*VerifyDetached)(const unsigned char*, const unsigned char*, unsigned long long, const unsigned char*) = nullptr;
			void (*Zero)(void*, std::size_t) = nullptr;
		};
	}
	std::shared_ptr<const ICryptoProvider> LoadSodiumCrypto(const std::string& Path, FDiagnostic& Error)
	{
		try
		{
			Detail::Physical(PathUtil::FromUtf8(Path));
			std::string Hash;
			if (!FileSha256(Path, Hash, Error)) return {};
			auto Provider = std::make_shared<FSodium>(Path);
			Provider->Initialize(std::move(Hash));
			Error = {};
			return Provider;
		}
		catch (const Detail::FFailure& E)
		{
			Error = E.Diagnostic;
			return {};
		}
		catch (const std::exception& E)
		{
			Error = {"crypto-load", E.what(), Path, {}};
			return {};
		}
	}
}
