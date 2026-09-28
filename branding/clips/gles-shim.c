/* libGLESv2.so.2 in front of Arm's libmali, for KWin in the GPU session.
 *
 * KWin's shaders test `#if GL_OES_standard_derivatives && ...`. Mesa reads an
 * undefined macro in #if as 0; Mali's GLSL ES preprocessor rejects it (and
 * reserves GL_ macro names, so it cannot be #defined), the shader fails and
 * KWin falls back to QPainter (no screencasts). This library defines
 * glShaderSource only: it rewrites that test to
 * `#if defined(GL_OES_standard_derivatives)` and passes the source on. Every
 * other GLES symbol resolves in libmali, which this library links (dlsym on
 * this handle searches its dependencies).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
typedef void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);

static const char FROM[] = "#if GL_OES_standard_derivatives";
static const char TO[] = "#if defined(GL_OES_standard_derivatives)";

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    static ShaderSource real;
    if (!real)
        real = (ShaderSource)dlsym(dlopen("libmali.so.1", RTLD_NOW | RTLD_NOLOAD), "glShaderSource");
    size_t total = 0, hits = 0;
    for (GLsizei i = 0; i < count; i++)
        total += length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
    char *src = malloc(total + 1);
    if (!src) {
        real(shader, count, string, length);
        return;
    }
    char *p = src;
    for (GLsizei i = 0; i < count; i++) {
        size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
        memcpy(p, string[i], n);
        p += n;
    }
    *p = '\0';
    for (p = strstr(src, FROM); p; p = strstr(p + 1, FROM))
        hits++;
    char *out = malloc(total + hits * (sizeof TO - sizeof FROM) + 1);
    if (!out) {
        free(src);
        real(shader, count, string, length);
        return;
    }
    char *o = out;
    const char *s = src;
    for (const char *q = strstr(s, FROM); q; q = strstr(s, FROM)) {
        memcpy(o, s, (size_t)(q - s));
        o += q - s;
        memcpy(o, TO, sizeof TO - 1);
        o += sizeof TO - 1;
        s = q + sizeof FROM - 1;
    }
    strcpy(o, s);
    const GLchar *one = out;
    real(shader, 1, &one, NULL);
    free(out);
    free(src);
}
