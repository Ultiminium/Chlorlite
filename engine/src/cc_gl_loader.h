#pragma once
/* cc_gl_loader.h — Windows OpenGL 3.3 function loader.
   On Windows, <GL/gl.h> exposes only GL 1.1; modern functions must be
   loaded at runtime. On Linux (Mesa) the gl.h prototypes are used directly,
   so this loader is a no-op there. Call cc_gl_load() after creating the GL
   context (glfwMakeContextCurrent). */
#ifdef _WIN32
#include <GL/gl.h>
#include <stdint.h>
/* GL 3.3 enum constants (absent from Windows gl.h) */
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_COLOR_ATTACHMENT2
#define GL_COLOR_ATTACHMENT2 0x8CE2
#endif
#ifndef GL_COLOR_ATTACHMENT3
#define GL_COLOR_ATTACHMENT3 0x8CE3
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif
#ifndef GL_TEXTURE4
#define GL_TEXTURE4 0x84C4
#endif
#ifndef GL_TEXTURE5
#define GL_TEXTURE5 0x84C5
#endif
#ifndef GL_TEXTURE6
#define GL_TEXTURE6 0x84C6
#endif
#ifndef GL_TEXTURE7
#define GL_TEXTURE7 0x84C7
#endif
#ifndef GL_TEXTURE8
#define GL_TEXTURE8 0x84C8
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#endif
#ifndef GL_TEXTURE_CUBE_MAP_POSITIVE_X
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
#ifndef GL_TEXTURE_CUBE_MAP_SEAMLESS
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_RGB16F
#define GL_RGB16F 0x881B
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RG16F
#define GL_RG16F 0x822F
#endif
#ifndef GL_HALF_FLOAT
#define GL_HALF_FLOAT 0x140B
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_DEPTH_STENCIL
#define GL_DEPTH_STENCIL 0x84F9
#endif
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_TEXTURE_2D_MULTISAMPLE
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#endif
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
#ifndef GL_DEPTH_STENCIL_TEXTURE_MODE
#define GL_DEPTH_STENCIL_TEXTURE_MODE 0x90EA
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_RGB16
#define GL_RGB16 0x8054
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
#ifndef GL_TEXTURE_BASE_LEVEL
#define GL_TEXTURE_BASE_LEVEL 0x813C
#endif

/* GL 3.x types missing from Windows gl.h */
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*PFN_glPolygonMode)(GLenum,GLenum);
extern PFN_glPolygonMode cc_glPolygonMode;
#define glPolygonMode cc_glPolygonMode
typedef void (*PFN_glActiveTexture)(GLenum texture);
extern PFN_glActiveTexture cc_glActiveTexture;
#define glActiveTexture cc_glActiveTexture
typedef void (*PFN_glAttachShader)(GLuint program, GLuint shader);
extern PFN_glAttachShader cc_glAttachShader;
#define glAttachShader cc_glAttachShader
typedef void (*PFN_glBindBuffer)(GLenum target, GLuint buffer);
extern PFN_glBindBuffer cc_glBindBuffer;
#define glBindBuffer cc_glBindBuffer
typedef void (*PFN_glBindBufferBase)(GLenum target, GLuint index, GLuint buffer);
extern PFN_glBindBufferBase cc_glBindBufferBase;
#define glBindBufferBase cc_glBindBufferBase
typedef void (*PFN_glBindFramebuffer)(GLenum target, GLuint framebuffer);
extern PFN_glBindFramebuffer cc_glBindFramebuffer;
#define glBindFramebuffer cc_glBindFramebuffer
typedef void (*PFN_glBlitFramebuffer)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
extern PFN_glBlitFramebuffer cc_glBlitFramebuffer;
#define glBlitFramebuffer cc_glBlitFramebuffer
typedef void (*PFN_glReadBuffer_cc)(GLenum);
extern PFN_glReadBuffer_cc cc_glReadBuffer;
#define glReadBuffer cc_glReadBuffer
typedef void (*PFN_glBindVertexArray)(GLuint array);
extern PFN_glBindVertexArray cc_glBindVertexArray;
#define glBindVertexArray cc_glBindVertexArray
typedef void (*PFN_glBufferData)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
extern PFN_glBufferData cc_glBufferData;
#define glBufferData cc_glBufferData
typedef void (*PFN_glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
extern PFN_glBufferSubData cc_glBufferSubData;
#define glBufferSubData cc_glBufferSubData
typedef GLenum (*PFN_glCheckFramebufferStatus)(GLenum target);
extern PFN_glCheckFramebufferStatus cc_glCheckFramebufferStatus;
#define glCheckFramebufferStatus cc_glCheckFramebufferStatus
typedef void (*PFN_glCompileShader)(GLuint shader);
extern PFN_glCompileShader cc_glCompileShader;
#define glCompileShader cc_glCompileShader
typedef GLuint (*PFN_glCreateProgram)(void);
extern PFN_glCreateProgram cc_glCreateProgram;
#define glCreateProgram cc_glCreateProgram
typedef GLuint (*PFN_glCreateShader)(GLenum type);
extern PFN_glCreateShader cc_glCreateShader;
#define glCreateShader cc_glCreateShader
typedef void (*PFN_glDeleteBuffers)(GLsizei n, const GLuint* buffers);
extern PFN_glDeleteBuffers cc_glDeleteBuffers;
#define glDeleteBuffers cc_glDeleteBuffers
typedef void (*PFN_glDeleteFramebuffers)(GLsizei n, const GLuint* framebuffers);
extern PFN_glDeleteFramebuffers cc_glDeleteFramebuffers;
#define glDeleteFramebuffers cc_glDeleteFramebuffers
typedef void (*PFN_glDeleteProgram)(GLuint program);
extern PFN_glDeleteProgram cc_glDeleteProgram;
#define glDeleteProgram cc_glDeleteProgram
typedef void (*PFN_glDeleteShader)(GLuint shader);
extern PFN_glDeleteShader cc_glDeleteShader;
#define glDeleteShader cc_glDeleteShader
typedef void (*PFN_glDeleteVertexArrays)(GLsizei n, const GLuint* arrays);
extern PFN_glDeleteVertexArrays cc_glDeleteVertexArrays;
#define glDeleteVertexArrays cc_glDeleteVertexArrays
typedef void (*PFN_glDrawBuffers)(GLsizei n, const GLenum* bufs);
extern PFN_glDrawBuffers cc_glDrawBuffers;
#define glDrawBuffers cc_glDrawBuffers
typedef void (*PFN_glEnableVertexAttribArray)(GLuint index);
extern PFN_glEnableVertexAttribArray cc_glEnableVertexAttribArray;
#define glEnableVertexAttribArray cc_glEnableVertexAttribArray
typedef void (*PFN_glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
extern PFN_glFramebufferTexture2D cc_glFramebufferTexture2D;
#define glFramebufferTexture2D cc_glFramebufferTexture2D
typedef void (*PFN_glGenBuffers)(GLsizei n, GLuint* buffers);
extern PFN_glGenBuffers cc_glGenBuffers;
#define glGenBuffers cc_glGenBuffers
typedef void (*PFN_glGenFramebuffers)(GLsizei n, GLuint* framebuffers);
extern PFN_glGenFramebuffers cc_glGenFramebuffers;
#define glGenFramebuffers cc_glGenFramebuffers
typedef void (*PFN_glGenVertexArrays)(GLsizei n, GLuint* arrays);
extern PFN_glGenVertexArrays cc_glGenVertexArrays;
#define glGenVertexArrays cc_glGenVertexArrays
typedef void (*PFN_glGenerateMipmap)(GLenum target);
extern PFN_glGenerateMipmap cc_glGenerateMipmap;
#define glGenerateMipmap cc_glGenerateMipmap
typedef void (*PFN_glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei* length, char* infoLog);
extern PFN_glGetProgramInfoLog cc_glGetProgramInfoLog;
#define glGetProgramInfoLog cc_glGetProgramInfoLog
typedef void (*PFN_glGetProgramiv)(GLuint program, GLenum pname, GLint* params);
extern PFN_glGetProgramiv cc_glGetProgramiv;
#define glGetProgramiv cc_glGetProgramiv
typedef void (*PFN_glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei* length, char* infoLog);
extern PFN_glGetShaderInfoLog cc_glGetShaderInfoLog;
#define glGetShaderInfoLog cc_glGetShaderInfoLog
typedef void (*PFN_glGetShaderiv)(GLuint shader, GLenum pname, GLint* params);
extern PFN_glGetShaderiv cc_glGetShaderiv;
#define glGetShaderiv cc_glGetShaderiv
typedef GLuint (*PFN_glGetUniformBlockIndex)(GLuint program, const char* uniformBlockName);
extern PFN_glGetUniformBlockIndex cc_glGetUniformBlockIndex;
#define glGetUniformBlockIndex cc_glGetUniformBlockIndex
typedef GLint (*PFN_glGetUniformLocation)(GLuint program, const char* name);
extern PFN_glGetUniformLocation cc_glGetUniformLocation;
#define glGetUniformLocation cc_glGetUniformLocation
typedef void (*PFN_glLinkProgram)(GLuint program);
extern PFN_glLinkProgram cc_glLinkProgram;
#define glLinkProgram cc_glLinkProgram
typedef void (*PFN_glShaderSource)(GLuint shader, GLsizei count, const char* const* string, const GLint* length);
extern PFN_glShaderSource cc_glShaderSource;
#define glShaderSource cc_glShaderSource
typedef void (*PFN_glUniform1f)(GLint location, GLfloat v0);
extern PFN_glUniform1f cc_glUniform1f;
#define glUniform1f cc_glUniform1f
typedef void (*PFN_glUniform1fv)(GLint location, GLsizei count, const GLfloat* value);
extern PFN_glUniform1fv cc_glUniform1fv;
#define glUniform1fv cc_glUniform1fv
typedef void (*PFN_glUniform1i)(GLint location, GLint v0);
extern PFN_glUniform1i cc_glUniform1i;
#define glUniform1i cc_glUniform1i
typedef void (*PFN_glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
extern PFN_glUniform2f cc_glUniform2f;
#define glUniform2f cc_glUniform2f
typedef void (*PFN_glUniform2fv)(GLint location, GLsizei count, const GLfloat* value);
extern PFN_glUniform2fv cc_glUniform2fv;
#define glUniform2fv cc_glUniform2fv
typedef void (*PFN_glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern PFN_glUniform3f cc_glUniform3f;
#define glUniform3f cc_glUniform3f
typedef void (*PFN_glUniform3fv)(GLint location, GLsizei count, const GLfloat* value);
extern PFN_glUniform3fv cc_glUniform3fv;
#define glUniform3fv cc_glUniform3fv
typedef void (*PFN_glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
extern PFN_glUniform4f cc_glUniform4f;
#define glUniform4f cc_glUniform4f
typedef void (*PFN_glUniform4fv)(GLint location, GLsizei count, const GLfloat* value);
extern PFN_glUniform4fv cc_glUniform4fv;
#define glUniform4fv cc_glUniform4fv
typedef void (*PFN_glUniformBlockBinding)(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
extern PFN_glUniformBlockBinding cc_glUniformBlockBinding;
#define glUniformBlockBinding cc_glUniformBlockBinding
typedef void (*PFN_glUniformMatrix3fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
extern PFN_glUniformMatrix3fv cc_glUniformMatrix3fv;
#define glUniformMatrix3fv cc_glUniformMatrix3fv
typedef void (*PFN_glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
extern PFN_glUniformMatrix4fv cc_glUniformMatrix4fv;
#define glUniformMatrix4fv cc_glUniformMatrix4fv
typedef void (*PFN_glUseProgram)(GLuint program);
extern PFN_glUseProgram cc_glUseProgram;
#define glUseProgram cc_glUseProgram
typedef void (*PFN_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
extern PFN_glVertexAttribPointer cc_glVertexAttribPointer;
#define glVertexAttribPointer cc_glVertexAttribPointer
typedef void (*PFN_glDisableVertexAttribArray)(GLuint index);
extern PFN_glDisableVertexAttribArray cc_glDisableVertexAttribArray;
#define glDisableVertexAttribArray cc_glDisableVertexAttribArray
typedef void (*PFN_glVertexAttribDivisor)(GLuint index, GLuint divisor);
extern PFN_glVertexAttribDivisor cc_glVertexAttribDivisor;
#define glVertexAttribDivisor cc_glVertexAttribDivisor
typedef void (*PFN_glDrawElementsInstanced)(GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount);
extern PFN_glDrawElementsInstanced cc_glDrawElementsInstanced;
#define glDrawElementsInstanced cc_glDrawElementsInstanced
typedef void (*PFN_glDrawArraysInstanced)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
extern PFN_glDrawArraysInstanced cc_glDrawArraysInstanced;
#define glDrawArraysInstanced cc_glDrawArraysInstanced
void cc_gl_load(void* (*getproc)(const char*));
#ifdef __cplusplus
}
#endif
#else
#define cc_gl_load(x) ((void)0)
#endif
