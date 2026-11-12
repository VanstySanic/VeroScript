// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSdl3GraphicsBackend.h"

#if VEROSCRIPT_SDL3_OPENGL
#define GL_GLEXT_PROTOTYPES
#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#else
#include <SDL3/SDL_opengl.h>
#endif
#include <array>
#include <cstring>

namespace VeroScript::Sdl3
{
	using namespace Graphics;
	namespace
	{
		#include "../VeroScriptSdl3Shaders.inl"
		// clang-format off
		#define VS_GL_FUNCTIONS(X) \
	X(GetFramebufferAttachmentParameteriv) \
	X(GetError) \
	X(GetString) \
	X(GetIntegerv) \
	X(GenTextures) \
	X(DeleteTextures) \
	X(BindTexture) \
	X(TexImage2D) \
	X(TexSubImage2D) \
	X(PixelStorei) \
	X(GenFramebuffers) \
	X(DeleteFramebuffers) \
	X(BindFramebuffer) \
	X(BindRenderbuffer) \
	X(FramebufferTexture2D) \
	X(CheckFramebufferStatus) \
	X(GenSamplers) \
	X(DeleteSamplers) \
	X(SamplerParameteri) \
	X(GenBuffers) \
	X(DeleteBuffers) \
	X(BindBuffer) \
	X(BufferData) \
	X(BufferSubData) \
	X(BindBufferBase) \
	X(CreateShader) \
	X(ShaderSource) \
	X(CompileShader) \
	X(GetShaderiv) \
	X(GetShaderInfoLog) \
	X(DeleteShader) \
	X(CreateProgram) \
	X(AttachShader) \
	X(LinkProgram) \
	X(GetProgramiv) \
	X(GetProgramInfoLog) \
	X(DeleteProgram) \
	X(GetUniformBlockIndex) \
	X(UniformBlockBinding) \
	X(GetUniformLocation) \
	X(Uniform1i) \
	X(UseProgram) \
	X(GenVertexArrays) \
	X(DeleteVertexArrays) \
	X(BindVertexArray) \
	X(EnableVertexAttribArray) \
	X(VertexAttribPointer) \
	X(ActiveTexture) \
	X(BindSampler) \
	X(Viewport) \
	X(Scissor) \
	X(Enable) \
	X(Disable) \
	X(ClearColor) \
	X(Clear) \
	X(BlendFuncSeparate) \
	X(BlendEquationSeparate) \
	X(ColorMask) \
	X(DrawElements) \
	X(ReadPixels) \
	X(Finish)
		// clang-format on
		struct FGl
		{
			#define VS_GL_DECLARE(Name) decltype(&gl##Name) Name = nullptr;
			VS_GL_FUNCTIONS(VS_GL_DECLARE)
			#undef VS_GL_DECLARE
			bool Load(std::string& Error)
			{
				#define VS_GL_LOAD(Name) \
					Name = reinterpret_cast<decltype(Name)>(SDL_GL_GetProcAddress("gl" #Name)); \
					if (!Name) \
					{ \
						return SdlError("load gl" #Name, Error); \
					}
				VS_GL_FUNCTIONS(VS_GL_LOAD)
				#undef VS_GL_LOAD
				return true;
			}
			bool Check(const char* Operation, std::string& E)
			{
				const auto Code = GetError();
				if (Code != GL_NO_ERROR)
				{
					E = std::string("graphics.opengl_failure: ") + Operation + " GL=" + std::to_string(Code);
					return false;
				}
				E.clear();
				return true;
			}
		};
		#undef VS_GL_FUNCTIONS
		struct FContext
		{
			~FContext()
			{
				if (Native)
				{
					SDL_GL_DestroyContext(Native);
				}
				if (HostWindow)
				{
					--HostWindow->OpenGlContextClaims;
				}
				if (Window && !HostWindow)
				{
					SDL_DestroyWindow(Window);
				}
			}
			FGl Gl;
			SDL_Window* Window = nullptr;
			SDL_GLContext Native = nullptr;
			std::shared_ptr<FWindowState> HostWindow;
			bool bEs = false;
		};
		struct FCurrent
		{
			FCurrent(FContext& C, SDL_Window* W = nullptr)
			    : Window(SDL_GL_GetCurrentWindow()), Context(SDL_GL_GetCurrentContext())
			{
				if (C.HostWindow && !C.HostWindow->bOpen.load())
				{
					return;
				}
				bValid = SDL_GL_MakeCurrent(W ? W : C.Window, C.Native);
			}
			~FCurrent()
			{
				if (bValid)
				{
					SDL_GL_MakeCurrent(Window, Context);
				}
			}
			SDL_Window* Window;
			SDL_GLContext Context;
			bool bValid = false;
		};
		struct FTexture final : ITextureData
		{
			~FTexture() override
			{
				FCurrent Current(*Context);
				if (!Current.bValid)
				{
					// Never delete numeric GL names in an unrelated host context.
					return;
				}
				if (Framebuffer)
				{
					Context->Gl.DeleteFramebuffers(1, &Framebuffer);
				}
				if (Texture)
				{
					Context->Gl.DeleteTextures(1, &Texture);
				}
			}
			FContext* Context = nullptr;
			FTextureOptions Options;
			GLuint Texture = 0, Framebuffer = 0;
		};
		struct FSampler final : ISamplerData
		{
			~FSampler() override
			{
				FCurrent Current(*Context);
				if (!Current.bValid)
				{
					return;
				}
				if (Sampler)
				{
					Context->Gl.DeleteSamplers(1, &Sampler);
				}
			}
			FContext* Context = nullptr;
			GLuint Sampler = 0;
		};
		struct FGeometry final : IGeometryData
		{
			~FGeometry() override
			{
				FCurrent Current(*Context);
				if (!Current.bValid)
				{
					return;
				}
				Context->Gl.DeleteBuffers(1, &Vertices);
				Context->Gl.DeleteBuffers(1, &Indices);
			}
			FContext* Context = nullptr;
			GLuint Vertices = 0, Indices = 0;
			GLsizei Count = 0;
		};
		struct FSurface final : ISurfaceData
		{
			SDL_Window* Window = nullptr;
			std::unique_ptr<ITextureData> Back;
			std::unique_ptr<IGeometryData> Quad;
			std::unique_ptr<ISamplerData> Sampler;
			int Width = 0, Height = 0;
		};
		class FOpenGl final : public IGraphicsBackend
		{
		public:
			~FOpenGl() override
			{
				if (!Context.Native)
				{
					return;
				}
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					return;
				}
				auto& G = Context.Gl;
				if (Program)
				{
					G.DeleteProgram(Program);
				}
				if (VertexUniform)
				{
					G.DeleteBuffers(1, &VertexUniform);
				}
				if (FragmentUniform)
				{
					G.DeleteBuffers(1, &FragmentUniform);
				}
				if (VertexArray)
				{
					G.DeleteVertexArrays(1, &VertexArray);
				}
			}
			const char* GetDriver() const noexcept override
			{
				return Context.bEs ? "opengles" : "opengl";
			}
			bool Poll(std::string& Error) override
			{
				if (IsLost())
				{
					Error = "graphics.device_lost: the OpenGL context window is closed";
					return false;
				}
				Error.clear();
				return true;
			}
			bool IsLost() const noexcept override
			{
				return Context.HostWindow && !Context.HostWindow->bOpen.load();
			}
			std::unique_ptr<ITextureData> CreateTexture(const FTextureOptions& O, std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					SdlError("activate OpenGL context", E);
					return {};
				}
				auto& G = Context.Gl;
				GLint Maximum = 0;
				G.GetIntegerv(GL_MAX_TEXTURE_SIZE, &Maximum);
				if (O.Width > Maximum || O.Height > Maximum)
				{
					E = "graphics.resource_limit: texture exceeds OpenGL dimensions";
					return {};
				}
				auto T = std::make_unique<FTexture>();
				T->Context = &Context;
				T->Options = O;
				G.GenTextures(1, &T->Texture);
				G.BindTexture(GL_TEXTURE_2D, T->Texture);
				G.TexImage2D(GL_TEXTURE_2D, 0, O.Format == ETextureFormat::Rgba8Srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
				    static_cast<GLsizei>(O.Width), static_cast<GLsizei>(O.Height), 0, GL_RGBA,
				    GL_UNSIGNED_BYTE, nullptr);
				G.GenFramebuffers(1, &T->Framebuffer);
				G.BindFramebuffer(GL_FRAMEBUFFER, T->Framebuffer);
				G.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, T->Texture, 0);
				if (G.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
				    !G.Check("create texture", E))
				{
					E = "graphics.opengl_failure: texture framebuffer is incomplete";
					return {};
				}
				return T;
			}
			std::unique_ptr<ISamplerData> CreateSampler(const FSamplerOptions& O, std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					SdlError("activate OpenGL context", E);
					return {};
				}
				auto S = std::make_unique<FSampler>();
				S->Context = &Context;
				auto& G = Context.Gl;
				G.GenSamplers(1, &S->Sampler);
				G.SamplerParameteri(S->Sampler, GL_TEXTURE_MIN_FILTER,
				    O.Filter == EFilter::Linear ? GL_LINEAR : GL_NEAREST);
				G.SamplerParameteri(S->Sampler, GL_TEXTURE_MAG_FILTER,
				    O.Filter == EFilter::Linear ? GL_LINEAR : GL_NEAREST);
				const auto Address = [](EAddressMode A)
				{
					return A == EAddressMode::Repeat ? GL_REPEAT
					    : A == EAddressMode::Mirror  ? GL_MIRRORED_REPEAT
					                                 : GL_CLAMP_TO_EDGE;
				};
				G.SamplerParameteri(S->Sampler, GL_TEXTURE_WRAP_S, Address(O.AddressU));
				G.SamplerParameteri(S->Sampler, GL_TEXTURE_WRAP_T, Address(O.AddressV));
				if (!G.Check("create sampler", E))
				{
					return {};
				}
				return S;
			}
			std::unique_ptr<IGeometryData> CreateGeometry(const std::vector<FVertex2D>& V,
			    const std::vector<std::uint32_t>& I, std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					SdlError("activate OpenGL context", E);
					return {};
				}
				auto R = std::make_unique<FGeometry>();
				R->Context = &Context;
				R->Count = static_cast<GLsizei>(I.size());
				auto& G = Context.Gl;
				G.BindVertexArray(VertexArray);
				G.GenBuffers(1, &R->Vertices);
				G.BindBuffer(GL_ARRAY_BUFFER, R->Vertices);
				G.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(V.size() * sizeof(FVertex2D)), V.data(),
				    GL_STATIC_DRAW);
				G.GenBuffers(1, &R->Indices);
				G.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, R->Indices);
				G.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(I.size() * sizeof(std::uint32_t)),
				    I.data(), GL_STATIC_DRAW);
				if (!G.Check("create geometry", E))
				{
					return {};
				}
				return R;
			}
			std::unique_ptr<ISurfaceData> CreateSurface(SDL_Window* W, std::string& E) override
			{
				if (Context.HostWindow && W != Context.Window)
				{
					E = "graphics.incompatible_window: this OpenGL context belongs to the selected host window";
					return {};
				}
				if (!(SDL_GetWindowFlags(W) & SDL_WINDOW_OPENGL))
				{
					E = "graphics.incompatible_window: create the window with GraphicsApi=opengl or opengles";
					return {};
				}
				FCurrent Current(Context, W);
				if (!Current.bValid)
				{
					SdlError("activate OpenGL surface", E);
					return {};
				}
				auto S = std::make_unique<FSurface>();
				S->Window = W;
				E.clear();
				return S;
			}
			bool UpdateTexture(ITextureData& T, const FRect& R, const std::vector<std::uint8_t>& P,
			    std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					return SdlError("activate OpenGL context", E);
				}
				auto& G = Context.Gl;
				G.BindTexture(GL_TEXTURE_2D, static_cast<FTexture&>(T).Texture);
				G.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
				G.TexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(R.X), static_cast<GLint>(R.Y),
				    static_cast<GLsizei>(R.Width), static_cast<GLsizei>(R.Height), GL_RGBA,
				    GL_UNSIGNED_BYTE, P.data());
				return G.Check("update texture", E);
			}
			bool ReadTexture(ITextureData& T, std::vector<std::uint8_t>& R, std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					return SdlError("activate OpenGL context", E);
				}
				auto& Source = static_cast<FTexture&>(T);
				auto& G = Context.Gl;
				std::vector<std::uint8_t> Copy(static_cast<std::size_t>(Source.Options.Width) * Source.Options.Height *
				    4);
				G.BindFramebuffer(GL_FRAMEBUFFER, Source.Framebuffer);
				G.PixelStorei(GL_PACK_ALIGNMENT, 1);
				G.ReadPixels(0, 0, static_cast<GLsizei>(Source.Options.Width),
				    static_cast<GLsizei>(Source.Options.Height), GL_RGBA, GL_UNSIGNED_BYTE, Copy.data());
				if (!G.Check("read texture", E))
				{
					return false;
				}
				R.swap(Copy);
				return true;
			}
			bool Render(ITextureData* T, ISurfaceData* S, const FRenderOptions2D& O, const FRect& V,
			    const std::vector<FBackendDraw>& Draws, bool& Presented, std::string& E) override
			{
				Presented = false;
				auto* Surface = S ? &static_cast<FSurface&>(*S) : nullptr;
				FCurrent Current(Context, Surface ? Surface->Window : nullptr);
				if (!Current.bValid)
				{
					return SdlError("activate OpenGL target", E);
				}
				auto& G = Context.Gl;
				int Height = 0, Width = 0;
				if (Surface && !SDL_GetWindowSizeInPixels(Surface->Window, &Width, &Height))
				{
					return SdlError("query OpenGL surface", E);
				}
				if (Surface &&
				    (Width <= 0 || Height <= 0 || (SDL_GetWindowFlags(Surface->Window) & SDL_WINDOW_MINIMIZED)))
				{
					E.clear();
					return true;
				}
				if (Surface && (!Surface->Back || Width != Surface->Width || Height != Surface->Height))
				{
					FTextureOptions Options{Width, Height, ETextureFormat::Rgba8Srgb};
					std::uint64_t Bytes = 0;
					if (!ValidateTextureOptions(Options, TextureLimit, Bytes, E))
					{
						return false;
					}
					auto Back = CreateTexture(Options, E);
					if (!Back)
					{
						return false;
					}
					const float W = static_cast<float>(Width), H = static_cast<float>(Height);
					auto Quad =
					    CreateGeometry({{0, 0, 0, 0}, {W, 0, 1, 0}, {W, H, 1, 1}, {0, H, 0, 1}}, {0, 1, 2, 0, 2, 3}, E);
					if (!Quad)
					{
						return false;
					}
					auto Sampler = CreateSampler({}, E);
					if (!Sampler)
					{
						return false;
					}
					Surface->Back = std::move(Back);
					Surface->Quad = std::move(Quad);
					Surface->Sampler = std::move(Sampler);
					Surface->Width = Width;
					Surface->Height = Height;
				}
				G.BindFramebuffer(GL_FRAMEBUFFER, Surface ? static_cast<FTexture*>(Surface->Back.get())->Framebuffer : static_cast<FTexture*>(T)->Framebuffer);
				#if defined(GL_FRAMEBUFFER_SRGB)
				if (!Context.bEs)
				{
					const bool Srgb = Surface || static_cast<FTexture*>(T)->Options.Format == ETextureFormat::Rgba8Srgb;
					Srgb ? G.Enable(GL_FRAMEBUFFER_SRGB) : G.Disable(GL_FRAMEBUFFER_SRGB);
				}
				#endif
				G.Disable(GL_DEPTH_TEST);
				G.Disable(GL_STENCIL_TEST);
				G.Disable(GL_CULL_FACE);
				G.Disable(GL_SCISSOR_TEST);
				G.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				if (O.bClear)
				{
					G.ClearColor(O.ClearColor.R, O.ClearColor.G, O.ClearColor.B, O.ClearColor.A);
					G.Clear(GL_COLOR_BUFFER_BIT);
				}
				G.Viewport(static_cast<GLint>(V.X), static_cast<GLint>(V.Y), static_cast<GLsizei>(V.Width),
				    static_cast<GLsizei>(V.Height));
				G.Enable(GL_SCISSOR_TEST);
				G.BindVertexArray(VertexArray);
				G.UseProgram(Program);
				G.BindBufferBase(GL_UNIFORM_BUFFER, 0, VertexUniform);
				G.BindBufferBase(GL_UNIFORM_BUFFER, 1, FragmentUniform);
				for (const auto& Draw : Draws)
				{
					DrawGeometry(Draw, V, 0, false, false);
				}
				if (!G.Check("render", E))
				{
					return false;
				}
				if (Surface)
				{
					// Blend into an sRGB texture first; default framebuffers need not support sRGB visuals.
					GLuint WindowFramebuffer = 0;
					#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
					const auto WindowProperties = SDL_GetWindowProperties(Surface->Window);
					WindowFramebuffer = static_cast<GLuint>(SDL_GetNumberProperty(
					    WindowProperties, SDL_PROP_WINDOW_UIKIT_OPENGL_FRAMEBUFFER_NUMBER, 0));
					#endif
					G.BindFramebuffer(GL_FRAMEBUFFER, WindowFramebuffer);
					bool Encode = true;
					if (Context.bEs)
					{
						GLint Encoding = GL_LINEAR;
						G.GetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, WindowFramebuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK,
						    GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &Encoding);
						Encode = Encoding != GL_SRGB;
					}
					#if defined(GL_FRAMEBUFFER_SRGB)
					else
					{
						G.Disable(GL_FRAMEBUFFER_SRGB);
					}
					#endif
					G.Viewport(0, 0, Width, Height);
					FDraw2D Command;
					Command.Blend = EBlendMode::Opaque;
					FBackendDraw Present{&Command,
					    Surface->Quad.get(),
					    Surface->Back.get(),
					    Surface->Back.get(),
					    Surface->Sampler.get(),
					    {Width, Height, ETextureFormat::Rgba8Srgb},
					    {0, 0, Width, Height}};
					DrawGeometry(Present, {0, 0, Width, Height}, Height, true, Encode);
					if (!G.Check("prepare OpenGL presentation", E))
					{
						return false;
					}

					#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS)
					G.BindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(SDL_GetNumberProperty(WindowProperties, SDL_PROP_WINDOW_UIKIT_OPENGL_RENDERBUFFER_NUMBER, 0)));
					#endif
					if (!SDL_GL_SwapWindow(Surface->Window))
					{
						return SdlError("present OpenGL surface", E);
					}
					Presented = true;
				}
				E.clear();
				return true;
			}
			bool WaitIdle(std::string& E) override
			{
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					return SdlError("activate OpenGL context", E);
				}
				Context.Gl.Finish();
				return Context.Gl.Check("wait idle", E);
			}
			bool Initialize(const std::shared_ptr<FRuntime>& Runtime, const FDeviceOptions& O, std::string& E)
			{
				(void)Runtime;
				Context.bEs = O.Driver == "opengles";
				TextureLimit = O.MaxTextureBytes;
				#if defined(SDL_PLATFORM_IOS) || defined(SDL_PLATFORM_TVOS) || defined(SDL_PLATFORM_ANDROID)
				// Mobile SDL backends permit one window; retain the host's window instead of creating a hidden one.
				if (!Context.bEs || !Runtime)
				{
					E = "graphics.unsupported_driver: mobile SDL requires an OpenGL ES context and an SDL runtime";
					return false;
				}
				std::shared_ptr<FWindowState> HostWindow;
				for (const auto& Entry : Runtime->Windows)
				{
					auto Window = Entry.second.lock();
					if (!Window || !Window->bOpen.load() ||
					    !(SDL_GetWindowFlags(Window->Retirement->Native) & SDL_WINDOW_OPENGL))
					{
						continue;
					}
					if (HostWindow)
					{
						E = "graphics.incompatible_window: mobile OpenGL ES requires exactly one compatible host window";
						return false;
					}
					HostWindow = std::move(Window);
				}
				if (!HostWindow)
				{
					E = "graphics.incompatible_window: create an opengles Platform window before the mobile device";
					return false;
				}
				#if defined(SDL_PLATFORM_ANDROID)
				// EGL contexts can share the one Android window. Keep the previous
				// device alive while a replacement is created or cancelled; surface
				// claims still prevent two devices from presenting at the same time.
				const bool bInUse = (HostWindow->Claims && !HostWindow->OpenGlContextClaims) ||
				    HostWindow->OpenGlContextClaims == UINT32_MAX;
				#else
				const bool bInUse = HostWindow->Claims || HostWindow->OpenGlContextClaims;
				#endif
				if (bInUse)
				{
					E = "graphics.surface_in_use: the mobile window already has a graphics owner";
					return false;
				}
				Context.HostWindow = std::move(HostWindow);
				++Context.HostWindow->OpenGlContextClaims;
				Context.Window = Context.HostWindow->Retirement->Native;
				#else
				Platform::FWindowOptions W;
				W.Title = "VeroScript graphics context";
				W.Width = W.Height = 1;
				W.bVisible = false;
				W.GraphicsApi = O.Driver;
				Context.Window = CreateNativeWindow(W, SDL_WINDOW_HIDDEN, E);
				if (!Context.Window)
				{
					return false;
				}
				#endif
				auto* PreviousWindow = SDL_GL_GetCurrentWindow();
				auto Previous = SDL_GL_GetCurrentContext();
				// Context creation uses the window's compatible pixel format and the requested profile.
				{
					struct FAttributes
					{
						~FAttributes()
						{
							if (bSaved)
							{
								for (unsigned I = 0; I < std::size(Keys); ++I)
								{
									SDL_GL_SetAttribute(Keys[I], Values[I]);
								}
							}
						}
						SDL_GLAttr Keys[5]{SDL_GL_CONTEXT_MAJOR_VERSION, SDL_GL_CONTEXT_MINOR_VERSION,
						    SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_FLAGS,
						    SDL_GL_SHARE_WITH_CURRENT_CONTEXT};
						int Values[5]{};
						bool bSaved = false;
					} Saved;
					for (unsigned I = 0; I < std::size(Saved.Keys); ++I)
					{
						if (!SDL_GL_GetAttribute(Saved.Keys[I], &Saved.Values[I]))
						{
							return SdlError("read OpenGL context configuration", E);
						}
					}
					Saved.bSaved = true;
					const int Values[]{3, Context.bEs ? 0 : 3,
					    Context.bEs ? SDL_GL_CONTEXT_PROFILE_ES : SDL_GL_CONTEXT_PROFILE_CORE,
					    O.bDebug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0, 0};
					for (unsigned I = 0; I < std::size(Saved.Keys); ++I)
					{
						if (!SDL_GL_SetAttribute(Saved.Keys[I], Values[I]))
						{
							return SdlError("configure OpenGL context", E);
						}
					}
					Context.Native = SDL_GL_CreateContext(Context.Window);
					if (!Context.Native)
					{
						SdlError("create OpenGL context", E);
					}
				}
				const bool bRestored = SDL_GL_MakeCurrent(PreviousWindow, Previous);
				if (!Context.Native)
				{
					return false;
				}
				if (!bRestored)
				{
					return SdlError("restore host OpenGL context", E);
				}
				FCurrent Current(Context);
				if (!Current.bValid)
				{
					return SdlError("activate OpenGL context", E);
				}
				if (!Context.Gl.Load(E))
				{
					return false;
				}
				auto& G = Context.Gl;
				const std::string Prefix = Context.bEs
				    ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
				    : "#version 330 core\n";
				const auto Compile = [&](GLenum Stage, const char* Source)
				{
					GLuint Shader = G.CreateShader(Stage);
					const char* Parts[]{Prefix.c_str(), Source};
					G.ShaderSource(Shader, 2, Parts, nullptr);
					G.CompileShader(Shader);
					GLint Status = 0;
					G.GetShaderiv(Shader, GL_COMPILE_STATUS, &Status);
					if (!Status)
					{
						char Log[4096]{};
						G.GetShaderInfoLog(Shader, sizeof(Log), nullptr, Log);
						E = std::string("graphics.opengl_shader: ") + Log;
						G.DeleteShader(Shader);
						return GLuint{0};
					}
					return Shader;
				};
				const auto Vertex = Compile(GL_VERTEX_SHADER, OpenGlVertex);
				if (!Vertex)
				{
					return false;
				}
				const auto Fragment = Compile(GL_FRAGMENT_SHADER, OpenGlFragment);
				if (!Fragment)
				{
					G.DeleteShader(Vertex);
					return false;
				}
				Program = G.CreateProgram();
				G.AttachShader(Program, Vertex);
				G.AttachShader(Program, Fragment);
				G.LinkProgram(Program);
				G.DeleteShader(Vertex);
				G.DeleteShader(Fragment);
				GLint Linked = 0;
				G.GetProgramiv(Program, GL_LINK_STATUS, &Linked);
				if (!Linked)
				{
					char Log[4096]{};
					G.GetProgramInfoLog(Program, sizeof(Log), nullptr, Log);
					E = std::string("graphics.opengl_link: ") + Log;
					return false;
				}
				G.UseProgram(Program);
				G.Uniform1i(G.GetUniformLocation(Program, "Image"), 0);
				G.Uniform1i(G.GetUniformLocation(Program, "Mask"), 1);
				G.UniformBlockBinding(Program, G.GetUniformBlockIndex(Program, "VertexParameters"), 0);
				G.UniformBlockBinding(Program, G.GetUniformBlockIndex(Program, "FragmentParameters"), 1);
				G.GenBuffers(1, &VertexUniform);
				G.BindBuffer(GL_UNIFORM_BUFFER, VertexUniform);
				G.BufferData(GL_UNIFORM_BUFFER, sizeof(FVertexUniform2D), nullptr, GL_DYNAMIC_DRAW);
				G.GenBuffers(1, &FragmentUniform);
				G.BindBuffer(GL_UNIFORM_BUFFER, FragmentUniform);
				G.BufferData(GL_UNIFORM_BUFFER, sizeof(FFragmentUniform2D), nullptr, GL_DYNAMIC_DRAW);
				G.GenVertexArrays(1, &VertexArray);
				return G.Check("initialize drawing", E);
			}

		private:
			void DrawGeometry(const FBackendDraw& Draw, const FRect& V, int Height, bool bSurface, bool bEncode)
			{
				auto& G = Context.Gl;
				auto U = MakeVertexUniform(Draw, V);
				U.Viewport[2] = bSurface ? 1.0f : 0.0f;
				auto F = MakeFragmentUniform(Draw);
				F.Output[0] = bEncode ? 1.0f : 0.0f;
				G.BindBuffer(GL_UNIFORM_BUFFER, VertexUniform);
				G.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(U), &U);
				G.BindBuffer(GL_UNIFORM_BUFFER, FragmentUniform);
				G.BufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(F), &F);
				G.Scissor(static_cast<GLint>(Draw.Clip.X),
				    static_cast<GLint>(bSurface ? Height - Draw.Clip.Y - Draw.Clip.Height : Draw.Clip.Y),
				    static_cast<GLsizei>(Draw.Clip.Width), static_cast<GLsizei>(Draw.Clip.Height));
				const auto Blend = Draw.Command->Blend;
				if (Blend == EBlendMode::Opaque)
				{
					G.Disable(GL_BLEND);
				}
				else
				{
					G.Enable(GL_BLEND);
					G.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
					const GLenum Dst = Blend == EBlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA;
					G.BlendFuncSeparate(Blend == EBlendMode::StraightAlpha ? GL_SRC_ALPHA : GL_ONE, Dst, GL_ONE, Dst);
				}
				const auto& Geometry = static_cast<FGeometry&>(*Draw.Geometry);
				G.BindBuffer(GL_ARRAY_BUFFER, Geometry.Vertices);
				G.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, Geometry.Indices);
				for (GLuint I = 0; I < 3; ++I)
				{
					G.EnableVertexAttribArray(I);
					G.VertexAttribPointer(I, I == 2 ? 4 : 2, GL_FLOAT, GL_FALSE, sizeof(FVertex2D),
					    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(I == 0 ? 0
					            : I == 1                                                 ? 8
					                                                                     : 16)));
				}
				G.ActiveTexture(GL_TEXTURE0);
				G.BindTexture(GL_TEXTURE_2D, static_cast<FTexture*>(Draw.Texture)->Texture);
				G.ActiveTexture(GL_TEXTURE1);
				G.BindTexture(GL_TEXTURE_2D, static_cast<FTexture*>(Draw.Mask)->Texture);
				const auto Sampler = static_cast<FSampler*>(Draw.Sampler)->Sampler;
				G.BindSampler(0, Sampler);
				G.BindSampler(1, Sampler);
				G.DrawElements(GL_TRIANGLES, Geometry.Count, GL_UNSIGNED_INT, nullptr);
			}
			FContext Context;
			std::uint64_t TextureLimit = 0;
			GLuint Program = 0, VertexUniform = 0, FragmentUniform = 0, VertexArray = 0;
		};
	}
	std::unique_ptr<IGraphicsBackend> CreateOpenGlBackend(const std::shared_ptr<FRuntime>& Runtime, const FDeviceOptions& O, std::string& E)
	{
		auto Driver = std::make_unique<FOpenGl>();
		if (!Driver->Initialize(Runtime, O, E))
		{
			return {};
		}
		return Driver;
	}
}
#else
namespace VeroScript::Sdl3
{
	std::unique_ptr<IGraphicsBackend> CreateOpenGlBackend(const std::shared_ptr<FRuntime>&, const Graphics::FDeviceOptions&, std::string& E)
	{
		E = "graphics.unsupported_driver: OpenGL/GLES is disabled in this build";
		return {};
	}
}
#endif
