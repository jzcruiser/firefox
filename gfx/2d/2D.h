/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _MOZILLA_GFX_2D_H
#define _MOZILLA_GFX_2D_H

#include "Types.h"
#include "Point.h"
#include "Rect.h"
#include "Matrix.h"
#include "UserData.h"

// GenericRefCountedBase allows us to hold on to refcounted objects of any type
// (contrary to RefCounted<T> which requires knowing the type T) and, in particular,
// without having a dependency on that type. This is used for DrawTargetSkia
// to be able to hold on to a GLContext.
#include "mozilla/GenericRefCounted.h"

// This RefPtr class isn't ideal for usage in Azure, as it doesn't allow T**
// outparams using the &-operator. But it will have to do as there's no easy
// solution.
#include "mozilla/RefPtr.h"

#ifdef MOZ_ENABLE_FREETYPE
#include <string>
#endif

struct _cairo_surface;
typedef _cairo_surface cairo_surface_t;

struct _cairo_scaled_font;
typedef _cairo_scaled_font cairo_scaled_font_t;

struct ID3D10Device1;
struct ID3D10Texture2D;
struct ID3D11Device;
struct ID2D1Device;
struct IDWriteRenderingParams;

class GrContext;
struct GrGLInterface;

struct CGContext;
typedef struct CGContext *CGContextRef;

namespace mozilla {

namespace gfx {

class SourceSurface;
class DataSourceSurface;
class DrawTarget;
class DrawEventRecorder;

struct NativeSurface {
  NativeSurfaceType mType;
  SurfaceFormat mFormat;
  void *mSurface;
};

struct NativeFont {
  NativeFontType mType;
  void *mFont;
};

/*
 * This structure is used to send draw options that are universal to all drawing
 * operations. It consists of the following:
 *
 * mAlpha         - Alpha value by which the mask generated by this operation is
 *                  multiplied.
 * mCompositionOp - The operator that indicates how the source and destination
 *                  patterns are blended.
 * mAntiAliasMode - The AntiAlias mode used for this drawing operation.
 * mSnapping      - Whether this operation is snapped to pixel boundaries.
 */
struct DrawOptions {
  DrawOptions(Float aAlpha = 1.0f,
              CompositionOp aCompositionOp = OP_OVER,
              AntialiasMode aAntialiasMode = AA_DEFAULT,
              Snapping aSnapping = SNAP_NONE)
    : mAlpha(aAlpha)
    , mCompositionOp(aCompositionOp)
    , mAntialiasMode(aAntialiasMode)
    , mSnapping(aSnapping)
  {}

  Float mAlpha;
  CompositionOp mCompositionOp : 8;
  AntialiasMode mAntialiasMode : 3;
  Snapping mSnapping : 1;
};

/*
 * This structure is used to send stroke options that are used in stroking
 * operations. It consists of the following:
 *
 * mLineWidth    - Width of the stroke in userspace.
 * mLineJoin     - Join style used for joining lines.
 * mLineCap      - Cap style used for capping lines.
 * mMiterLimit   - Miter limit in units of linewidth
 * mDashPattern  - Series of on/off userspace lengths defining dash.
 *                 Owned by the caller; must live at least as long as
 *                 this StrokeOptions.
 *                 mDashPattern != null <=> mDashLength > 0.
 * mDashLength   - Number of on/off lengths in mDashPattern.
 * mDashOffset   - Userspace offset within mDashPattern at which stroking
 *                 begins.
 */
struct StrokeOptions {
  StrokeOptions(Float aLineWidth = 1.0f,
                JoinStyle aLineJoin = JOIN_MITER_OR_BEVEL,
                CapStyle aLineCap = CAP_BUTT,
                Float aMiterLimit = 10.0f,
                size_t aDashLength = 0,
                const Float* aDashPattern = 0,
                Float aDashOffset = 0.f)
    : mLineWidth(aLineWidth)
    , mMiterLimit(aMiterLimit)
    , mDashPattern(aDashLength > 0 ? aDashPattern : 0)
    , mDashLength(aDashLength)
    , mDashOffset(aDashOffset)
    , mLineJoin(aLineJoin)
    , mLineCap(aLineCap)
  {
    MOZ_ASSERT(aDashLength == 0 || aDashPattern);
  }

  Float mLineWidth;
  Float mMiterLimit;
  const Float* mDashPattern;
  size_t mDashLength;
  Float mDashOffset;
  JoinStyle mLineJoin : 4;
  CapStyle mLineCap : 3;
};

/*
 * This structure supplies additional options for calls to DrawSurface.
 *
 * mFilter - Filter used when resampling source surface region to the
 *           destination region.
 * aSamplingBounds - This indicates whether the implementation is allowed
 *                   to sample pixels outside the source rectangle as
 *                   specified in DrawSurface on the surface.
 */
struct DrawSurfaceOptions {
  DrawSurfaceOptions(Filter aFilter = FILTER_LINEAR,
                     SamplingBounds aSamplingBounds = SAMPLING_UNBOUNDED)
    : mFilter(aFilter)
    , mSamplingBounds(aSamplingBounds)
  { }

  Filter mFilter : 3;
  SamplingBounds mSamplingBounds : 1;
};

/*
 * This class is used to store gradient stops, it can only be used with a
 * matching DrawTarget. Not adhering to this condition will make a draw call
 * fail.
 */
class GradientStops : public RefCounted<GradientStops>
{
public:
  virtual ~GradientStops() {}

  virtual BackendType GetBackendType() const = 0;

protected:
  GradientStops() {}
};

/*
 * This is the base class for 'patterns'. Patterns describe the pixels used as
 * the source for a masked composition operation that is done by the different
 * drawing commands. These objects are not backend specific, however for
 * example the gradient stops on a gradient pattern can be backend specific.
 */
class Pattern
{
public:
  virtual ~Pattern() {}

  virtual PatternType GetType() const = 0;

protected:
  Pattern() {}
};

class ColorPattern : public Pattern
{
public:
  ColorPattern(const Color &aColor)
    : mColor(aColor)
  {}

  virtual PatternType GetType() const { return PATTERN_COLOR; }

  Color mColor;
};

/*
 * This class is used for Linear Gradient Patterns, the gradient stops are
 * stored in a separate object and are backend dependent. This class itself
 * may be used on the stack.
 */
class LinearGradientPattern : public Pattern
{
public:
  /*
   * aBegin Start of the linear gradient
   * aEnd End of the linear gradient - NOTE: In the case of a zero length
   *      gradient it will act as the color of the last stop.
   * aStops GradientStops object for this gradient, this should match the
   *        backend type of the draw target this pattern will be used with.
   * aMatrix A matrix that transforms the pattern into user space
   */
  LinearGradientPattern(const Point &aBegin,
                        const Point &aEnd,
                        GradientStops *aStops,
                        const Matrix &aMatrix = Matrix())
    : mBegin(aBegin)
    , mEnd(aEnd)
    , mStops(aStops)
    , mMatrix(aMatrix)
  {
  }

  virtual PatternType GetType() const { return PATTERN_LINEAR_GRADIENT; }

  Point mBegin;
  Point mEnd;
  RefPtr<GradientStops> mStops;
  Matrix mMatrix;
};

/*
 * This class is used for Radial Gradient Patterns, the gradient stops are
 * stored in a separate object and are backend dependent. This class itself
 * may be used on the stack.
 */
class RadialGradientPattern : public Pattern
{
public:
  /*
   * aCenter1 Center of the inner (focal) circle.
   * aCenter2 Center of the outer circle.
   * aRadius1 Radius of the inner (focal) circle.
   * aRadius2 Radius of the outer circle.
   * aStops GradientStops object for this gradient, this should match the
   *        backend type of the draw target this pattern will be used with.
   * aMatrix A matrix that transforms the pattern into user space
   */
  RadialGradientPattern(const Point &aCenter1,
                        const Point &aCenter2,
                        Float aRadius1,
                        Float aRadius2,
                        GradientStops *aStops,
                        const Matrix &aMatrix = Matrix())
    : mCenter1(aCenter1)
    , mCenter2(aCenter2)
    , mRadius1(aRadius1)
    , mRadius2(aRadius2)
    , mStops(aStops)
    , mMatrix(aMatrix)
  {
  }

  virtual PatternType GetType() const { return PATTERN_RADIAL_GRADIENT; }

  Point mCenter1;
  Point mCenter2;
  Float mRadius1;
  Float mRadius2;
  RefPtr<GradientStops> mStops;
  Matrix mMatrix;
};

/*
 * This class is used for Surface Patterns, they wrap a surface and a
 * repetition mode for the surface. This may be used on the stack.
 */
class SurfacePattern : public Pattern
{
public:
  /*
   * aSourceSurface Surface to use for drawing
   * aExtendMode This determines how the image is extended outside the bounds
   *             of the image.
   * aMatrix A matrix that transforms the pattern into user space
   * aFilter Resampling filter used for resampling the image.
   */
  SurfacePattern(SourceSurface *aSourceSurface, ExtendMode aExtendMode,
                 const Matrix &aMatrix = Matrix(), Filter aFilter = FILTER_GOOD)
    : mSurface(aSourceSurface)
    , mExtendMode(aExtendMode)
    , mFilter(aFilter)
    , mMatrix(aMatrix)
  {}

  virtual PatternType GetType() const { return PATTERN_SURFACE; }

  RefPtr<SourceSurface> mSurface;
  ExtendMode mExtendMode;
  Filter mFilter;
  Matrix mMatrix;
};

/*
 * This is the base class for source surfaces. These objects are surfaces
 * which may be used as a source in a SurfacePattern or a DrawSurface call.
 * They cannot be drawn to directly.
 */
class SourceSurface : public RefCounted<SourceSurface>
{
public:
  virtual ~SourceSurface() {}

  virtual SurfaceType GetType() const = 0;
  virtual IntSize GetSize() const = 0;
  virtual SurfaceFormat GetFormat() const = 0;

  /* This returns false if some event has made this source surface invalid for
   * usage with current DrawTargets. For example in the case of Direct2D this
   * could return false if we have switched devices since this surface was
   * created.
   */
  virtual bool IsValid() const { return true; }

  /*
   * This function will get a DataSourceSurface for this surface, a
   * DataSourceSurface's data can be accessed directly.
   */
  virtual TemporaryRef<DataSourceSurface> GetDataSurface() = 0;
};

class DataSourceSurface : public SourceSurface
{
public:
  virtual SurfaceType GetType() const { return SURFACE_DATA; }
  /*
   * Get the raw bitmap data of the surface.
   * Can return null if there was OOM allocating surface data.
   */
  virtual uint8_t *GetData() = 0;

  /*
   * Stride of the surface, distance in bytes between the start of the image
   * data belonging to row y and row y+1. This may be negative.
   * Can return 0 if there was OOM allocating surface data.
   */
  virtual int32_t Stride() = 0;

  /*
   * This function is called after modifying the data on the source surface
   * directly through the data pointer.
   */
  virtual void MarkDirty() {}

  virtual TemporaryRef<DataSourceSurface> GetDataSurface() { RefPtr<DataSourceSurface> temp = this; return temp.forget(); }
};

/* This is an abstract object that accepts path segments. */
class PathSink : public RefCounted<PathSink>
{
public:
  virtual ~PathSink() {}

  /* Move the current point in the path, any figure currently being drawn will
   * be considered closed during fill operations, however when stroking the
   * closing line segment will not be drawn.
   */
  virtual void MoveTo(const Point &aPoint) = 0;
  /* Add a linesegment to the current figure */
  virtual void LineTo(const Point &aPoint) = 0;
  /* Add a cubic bezier curve to the current figure */
  virtual void BezierTo(const Point &aCP1,
                        const Point &aCP2,
                        const Point &aCP3) = 0;
  /* Add a quadratic bezier curve to the current figure */
  virtual void QuadraticBezierTo(const Point &aCP1,
                                 const Point &aCP2) = 0;
  /* Close the current figure, this will essentially generate a line segment
   * from the current point to the starting point for the current figure
   */
  virtual void Close() = 0;
  /* Add an arc to the current figure */
  virtual void Arc(const Point &aOrigin, float aRadius, float aStartAngle,
                   float aEndAngle, bool aAntiClockwise = false) = 0;
  /* Point the current subpath is at - or where the next subpath will start
   * if there is no active subpath.
   */
  virtual Point CurrentPoint() const = 0;
};

class PathBuilder;

/* The path class is used to create (sets of) figures of any shape that can be
 * filled or stroked to a DrawTarget
 */
class Path : public RefCounted<Path>
{
public:
  virtual ~Path() {}
  
  virtual BackendType GetBackendType() const = 0;

  /* This returns a PathBuilder object that contains a copy of the contents of
   * this path and is still writable.
   */
  virtual TemporaryRef<PathBuilder> CopyToBuilder(FillRule aFillRule = FILL_WINDING) const = 0;
  virtual TemporaryRef<PathBuilder> TransformedCopyToBuilder(const Matrix &aTransform,
                                                             FillRule aFillRule = FILL_WINDING) const = 0;

  /* This function checks if a point lies within a path. It allows passing a
   * transform that will transform the path to the coordinate space in which
   * aPoint is given.
   */
  virtual bool ContainsPoint(const Point &aPoint, const Matrix &aTransform) const = 0;


  /* This function checks if a point lies within the stroke of a path using the
   * specified strokeoptions. It allows passing a transform that will transform
   * the path to the coordinate space in which aPoint is given.
   */
  virtual bool StrokeContainsPoint(const StrokeOptions &aStrokeOptions,
                                   const Point &aPoint,
                                   const Matrix &aTransform) const = 0;

  /* This functions gets the bounds of this path. These bounds are not
   * guaranteed to be tight. A transform may be specified that gives the bounds
   * after application of the transform.
   */
  virtual Rect GetBounds(const Matrix &aTransform = Matrix()) const = 0;

  /* This function gets the bounds of the stroke of this path using the
   * specified strokeoptions. These bounds are not guaranteed to be tight.
   * A transform may be specified that gives the bounds after application of
   * the transform.
   */
  virtual Rect GetStrokedBounds(const StrokeOptions &aStrokeOptions,
                                const Matrix &aTransform = Matrix()) const = 0;

  /* This gets the fillrule this path's builder was created with. This is not
   * mutable.
   */
  virtual FillRule GetFillRule() const = 0;

  virtual Float ComputeLength() { return 0; }

  virtual Point ComputePointAtLength(Float aLength,
                                     Point* aTangent) { return Point(); }
};

/* The PathBuilder class allows path creation. Once finish is called on the
 * pathbuilder it may no longer be written to.
 */
class PathBuilder : public PathSink
{
public:
  /* Finish writing to the path and return a Path object that can be used for
   * drawing. Future use of the builder results in a crash!
   */
  virtual TemporaryRef<Path> Finish() = 0;
};

struct Glyph
{
  uint32_t mIndex;
  Point mPosition;
};

/* This class functions as a glyph buffer that can be drawn to a DrawTarget.
 * XXX - This should probably contain the guts of gfxTextRun in the future as
 * roc suggested. But for now it's a simple container for a glyph vector.
 */
struct GlyphBuffer
{
  // A pointer to a buffer of glyphs. Managed by the caller.
  const Glyph *mGlyphs;
  // Number of glyphs mGlyphs points to.
  uint32_t mNumGlyphs;
};

/* This class is an abstraction of a backend/platform specific font object
 * at a particular size. It is passed into text drawing calls to describe
 * the font used for the drawing call.
 */
class ScaledFont : public RefCounted<ScaledFont>
{
public:
  virtual ~ScaledFont() {}

  typedef void (*FontFileDataOutput)(const uint8_t *aData, uint32_t aLength, uint32_t aIndex, Float aGlyphSize, void *aBaton);

  virtual FontType GetType() const = 0;

  /* This allows getting a path that describes the outline of a set of glyphs.
   * A target is passed in so that the guarantee is made the returned path
   * can be used with any DrawTarget that has the same backend as the one
   * passed in.
   */
  virtual TemporaryRef<Path> GetPathForGlyphs(const GlyphBuffer &aBuffer, const DrawTarget *aTarget) = 0;

  /* This copies the path describing the glyphs into a PathBuilder. We use this
   * API rather than a generic API to append paths because it allows easier
   * implementation in some backends, and more efficient implementation in
   * others.
   */
  virtual void CopyGlyphsToBuilder(const GlyphBuffer &aBuffer, PathBuilder *aBuilder, const Matrix *aTransformHint = nullptr) = 0;

  virtual bool GetFontFileData(FontFileDataOutput, void *) { return false; }

  void AddUserData(UserDataKey *key, void *userData, void (*destroy)(void*)) {
    mUserData.Add(key, userData, destroy);
  }
  void *GetUserData(UserDataKey *key) {
    return mUserData.Get(key);
  }

protected:
  ScaledFont() {}

  UserData mUserData;
};

#ifdef MOZ_ENABLE_FREETYPE
/**
 * Describes a font
 * Used to pass the key informatin from a gfxFont into Azure
 * XXX Should be replaced by a more long term solution, perhaps Bug 738014
 */
struct FontOptions
{
  std::string mName;
  FontStyle mStyle;
};
#endif


/* This class is designed to allow passing additional glyph rendering
 * parameters to the glyph drawing functions. This is an empty wrapper class
 * merely used to allow holding on to and passing around platform specific
 * parameters. This is because different platforms have unique rendering
 * parameters.
 */
class GlyphRenderingOptions : public RefCounted<GlyphRenderingOptions>
{
public:
  virtual ~GlyphRenderingOptions() {}

  virtual FontType GetType() const = 0;

protected:
  GlyphRenderingOptions() {}
};

/* This is the main class used for all the drawing. It is created through the
 * factory and accepts drawing commands. The results of drawing to a target
 * may be used either through a Snapshot or by flushing the target and directly
 * accessing the backing store a DrawTarget was created with.
 */
class DrawTarget : public RefCounted<DrawTarget>
{
public:
  DrawTarget() : mTransformDirty(false), mPermitSubpixelAA(false) {}
  virtual ~DrawTarget() {}

  virtual BackendType GetType() const = 0;
  /**
   * Returns a SourceSurface which is a snapshot of the current contents of the DrawTarget.
   * Multiple calls to Snapshot() without any drawing operations in between will
   * normally return the same SourceSurface object.
   */
  virtual TemporaryRef<SourceSurface> Snapshot() = 0;
  virtual IntSize GetSize() = 0;

  /**
   * If possible returns the bits to this DrawTarget for direct manipulation. While
   * the bits is locked any modifications to this DrawTarget is forbidden.
   * Release takes the original data pointer for safety.
   */
  virtual bool LockBits(uint8_t** aData, IntSize* aSize,
                        int32_t* aStride, SurfaceFormat* aFormat) { return false; }
  virtual void ReleaseBits(uint8_t* aData) {}

  /* Ensure that the DrawTarget backend has flushed all drawing operations to
   * this draw target. This must be called before using the backing surface of
   * this draw target outside of GFX 2D code.
   */
  virtual void Flush() = 0;

  /*
   * Draw a surface to the draw target. Possibly doing partial drawing or
   * applying scaling. No sampling happens outside the source.
   *
   * aSurface Source surface to draw
   * aDest Destination rectangle that this drawing operation should draw to
   * aSource Source rectangle in aSurface coordinates, this area of aSurface
   *         will be stretched to the size of aDest.
   * aOptions General draw options that are applied to the operation
   * aSurfOptions DrawSurface options that are applied
   */
  virtual void DrawSurface(SourceSurface *aSurface,
                           const Rect &aDest,
                           const Rect &aSource,
                           const DrawSurfaceOptions &aSurfOptions = DrawSurfaceOptions(),
                           const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Blend a surface to the draw target with a shadow. The shadow is drawn as a
   * gaussian blur using a specified sigma. The shadow is clipped to the size
   * of the input surface, so the input surface should contain a transparent
   * border the size of the approximate coverage of the blur (3 * aSigma).
   * NOTE: This function works in device space!
   *
   * aSurface Source surface to draw.
   * aDest Destination point that this drawing operation should draw to.
   * aColor Color of the drawn shadow
   * aOffset Offset of the shadow
   * aSigma Sigma used for the guassian filter kernel
   * aOperator Composition operator used
   */
  virtual void DrawSurfaceWithShadow(SourceSurface *aSurface,
                                     const Point &aDest,
                                     const Color &aColor,
                                     const Point &aOffset,
                                     Float aSigma,
                                     CompositionOp aOperator) = 0;

  /* 
   * Clear a rectangle on the draw target to transparent black. This will
   * respect the clipping region and transform.
   *
   * aRect Rectangle to clear
   */
  virtual void ClearRect(const Rect &aRect) = 0;

  /*
   * This is essentially a 'memcpy' between two surfaces. It moves a pixel
   * aligned area from the source surface unscaled directly onto the
   * drawtarget. This ignores both transform and clip.
   *
   * aSurface Surface to copy from
   * aSourceRect Source rectangle to be copied
   * aDest Destination point to copy the surface to
   */
  virtual void CopySurface(SourceSurface *aSurface,
                           const IntRect &aSourceRect,
                           const IntPoint &aDestination) = 0;

  /*
   * Same as CopySurface, except uses itself as the source.
   *
   * Some backends may be able to optimize this better
   * than just taking a snapshot and using CopySurface.
   */
  virtual void CopyRect(const IntRect &aSourceRect,
                        const IntPoint &aDestination)
  {
    RefPtr<SourceSurface> source = Snapshot();
    CopySurface(source, aSourceRect, aDestination);
  }

  /*
   * Fill a rectangle on the DrawTarget with a certain source pattern.
   *
   * aRect Rectangle that forms the mask of this filling operation
   * aPattern Pattern that forms the source of this filling operation
   * aOptions Options that are applied to this operation
   */
  virtual void FillRect(const Rect &aRect,
                        const Pattern &aPattern,
                        const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Stroke a rectangle on the DrawTarget with a certain source pattern.
   *
   * aRect Rectangle that forms the mask of this stroking operation
   * aPattern Pattern that forms the source of this stroking operation
   * aOptions Options that are applied to this operation
   */
  virtual void StrokeRect(const Rect &aRect,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions = StrokeOptions(),
                          const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Stroke a line on the DrawTarget with a certain source pattern.
   *
   * aStart Starting point of the line
   * aEnd End point of the line
   * aPattern Pattern that forms the source of this stroking operation
   * aOptions Options that are applied to this operation
   */
  virtual void StrokeLine(const Point &aStart,
                          const Point &aEnd,
                          const Pattern &aPattern,
                          const StrokeOptions &aStrokeOptions = StrokeOptions(),
                          const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Stroke a path on the draw target with a certain source pattern.
   *
   * aPath Path that is to be stroked
   * aPattern Pattern that should be used for the stroke
   * aStrokeOptions Stroke options used for this operation
   * aOptions Draw options used for this operation
   */
  virtual void Stroke(const Path *aPath,
                      const Pattern &aPattern,
                      const StrokeOptions &aStrokeOptions = StrokeOptions(),
                      const DrawOptions &aOptions = DrawOptions()) = 0;
  
  /*
   * Fill a path on the draw target with a certain source pattern.
   *
   * aPath Path that is to be filled
   * aPattern Pattern that should be used for the fill
   * aOptions Draw options used for this operation
   */
  virtual void Fill(const Path *aPath,
                    const Pattern &aPattern,
                    const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Fill a series of clyphs on the draw target with a certain source pattern.
   */
  virtual void FillGlyphs(ScaledFont *aFont,
                          const GlyphBuffer &aBuffer,
                          const Pattern &aPattern,
                          const DrawOptions &aOptions = DrawOptions(),
                          const GlyphRenderingOptions *aRenderingOptions = nullptr) = 0;

  /*
   * This takes a source pattern and a mask, and composites the source pattern
   * onto the destination surface using the alpha channel of the mask pattern
   * as a mask for the operation.
   *
   * aSource Source pattern
   * aMask Mask pattern
   * aOptions Drawing options
   */
  virtual void Mask(const Pattern &aSource,
                    const Pattern &aMask,
                    const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * This takes a source pattern and a mask, and composites the source pattern
   * onto the destination surface using the alpha channel of the mask source.
   * The operation is bound by the extents of the mask.
   *
   * aSource Source pattern
   * aMask Mask surface
   * aOffset a transformed offset that the surface is masked at
   * aOptions Drawing options
   */
  virtual void MaskSurface(const Pattern &aSource,
                           SourceSurface *aMask,
                           Point aOffset,
                           const DrawOptions &aOptions = DrawOptions()) = 0;

  /*
   * Push a clip to the DrawTarget.
   *
   * aPath The path to clip to
   */
  virtual void PushClip(const Path *aPath) = 0;

  /*
   * Push an axis-aligned rectangular clip to the DrawTarget. This rectangle
   * is specified in user space.
   *
   * aRect The rect to clip to
   */
  virtual void PushClipRect(const Rect &aRect) = 0;

  /* Pop a clip from the DrawTarget. A pop without a corresponding push will
   * be ignored.
   */
  virtual void PopClip() = 0;

  /*
   * Create a SourceSurface optimized for use with this DrawTarget from
   * existing bitmap data in memory.
   *
   * The SourceSurface does not take ownership of aData, and may be freed at any time.
   */
  virtual TemporaryRef<SourceSurface> CreateSourceSurfaceFromData(unsigned char *aData,
                                                                  const IntSize &aSize,
                                                                  int32_t aStride,
                                                                  SurfaceFormat aFormat) const = 0;

  /*
   * Create a SourceSurface optimized for use with this DrawTarget from
   * an arbitrary other SourceSurface. This may return aSourceSurface or some
   * other existing surface.
   */
  virtual TemporaryRef<SourceSurface> OptimizeSourceSurface(SourceSurface *aSurface) const = 0;

  /*
   * Create a SourceSurface for a type of NativeSurface. This may fail if the
   * draw target does not know how to deal with the type of NativeSurface passed
   * in.
   */
  virtual TemporaryRef<SourceSurface>
    CreateSourceSurfaceFromNativeSurface(const NativeSurface &aSurface) const = 0;

  /*
   * Create a DrawTarget whose snapshot is optimized for use with this DrawTarget.
   */
  virtual TemporaryRef<DrawTarget>
    CreateSimilarDrawTarget(const IntSize &aSize, SurfaceFormat aFormat) const = 0;

  /*
   * Create a draw target optimized for drawing a shadow.
   *
   * Note that aSigma is the blur radius that must be used when we draw the
   * shadow. Also note that this doesn't affect the size of the allocated
   * surface, the caller is still responsible for including the shadow area in
   * its size.
   */
  virtual TemporaryRef<DrawTarget>
    CreateShadowDrawTarget(const IntSize &aSize, SurfaceFormat aFormat,
                           float aSigma) const
  {
    return CreateSimilarDrawTarget(aSize, aFormat);
  }

  /*
   * Create a path builder with the specified fillmode.
   *
   * We need the fill mode up front because of Direct2D.
   * ID2D1SimplifiedGeometrySink requires the fill mode
   * to be set before calling BeginFigure().
   */
  virtual TemporaryRef<PathBuilder> CreatePathBuilder(FillRule aFillRule = FILL_WINDING) const = 0;

  /*
   * Create a GradientStops object that holds information about a set of
   * gradient stops, this object is required for linear or radial gradient
   * patterns to represent the color stops in the gradient.
   *
   * aStops An array of gradient stops
   * aNumStops Number of stops in the array aStops
   * aExtendNone This describes how to extend the stop color outside of the
   *             gradient area.
   */
  virtual TemporaryRef<GradientStops>
    CreateGradientStops(GradientStop *aStops,
                        uint32_t aNumStops,
                        ExtendMode aExtendMode = EXTEND_CLAMP) const = 0;

  const Matrix &GetTransform() const { return mTransform; }

  /*
   * Set a transform on the surface, this transform is applied at drawing time
   * to both the mask and source of the operation.
   */
  virtual void SetTransform(const Matrix &aTransform)
    { mTransform = aTransform; mTransformDirty = true; }

  SurfaceFormat GetFormat() { return mFormat; }

  /* Tries to get a native surface for a DrawTarget, this may fail if the
   * draw target cannot convert to this surface type.
   */
  virtual void *GetNativeSurface(NativeSurfaceType aType) { return nullptr; }

  virtual bool IsDualDrawTarget() { return false; }

  void AddUserData(UserDataKey *key, void *userData, void (*destroy)(void*)) {
    mUserData.Add(key, userData, destroy);
  }
  void *GetUserData(UserDataKey *key) {
    return mUserData.Get(key);
  }

  /* Within this rectangle all pixels will be opaque by the time the result of
   * this DrawTarget is first used for drawing. Either by the underlying surface
   * being used as an input to external drawing, or Snapshot() being called.
   * This rectangle is specified in device space.
   */
  void SetOpaqueRect(const IntRect &aRect) {
    mOpaqueRect = aRect;
  }

  const IntRect &GetOpaqueRect() const {
    return mOpaqueRect;
  }

  virtual void SetPermitSubpixelAA(bool aPermitSubpixelAA) {
    mPermitSubpixelAA = aPermitSubpixelAA;
  }

  bool GetPermitSubpixelAA() {
    return mPermitSubpixelAA;
  }

  virtual GenericRefCountedBase* GetGLContext() const {
    return nullptr;
  }

#ifdef USE_SKIA_GPU
  virtual void InitWithGLContextAndGrGLInterface(GenericRefCountedBase* aGLContext,
                                            GrGLInterface* aGrGLInterface,
                                            const IntSize &aSize,
                                            SurfaceFormat aFormat)
  {
    MOZ_CRASH();
  }
#endif

protected:
  UserData mUserData;
  Matrix mTransform;
  IntRect mOpaqueRect;
  bool mTransformDirty : 1;
  bool mPermitSubpixelAA : 1;

  SurfaceFormat mFormat;
};

class DrawEventRecorder : public RefCounted<DrawEventRecorder>
{
public:
  virtual ~DrawEventRecorder() { }
};

class GFX2D_API Factory
{
public:
  static bool HasSSE2();

  static TemporaryRef<DrawTarget> CreateDrawTargetForCairoSurface(cairo_surface_t* aSurface, const IntSize& aSize);

  static TemporaryRef<DrawTarget>
    CreateDrawTarget(BackendType aBackend, const IntSize &aSize, SurfaceFormat aFormat);

  static TemporaryRef<DrawTarget>
    CreateRecordingDrawTarget(DrawEventRecorder *aRecorder, DrawTarget *aDT);
     
  static TemporaryRef<DrawTarget>
    CreateDrawTargetForData(BackendType aBackend, unsigned char* aData, const IntSize &aSize, int32_t aStride, SurfaceFormat aFormat);

  static TemporaryRef<ScaledFont>
    CreateScaledFontForNativeFont(const NativeFont &aNativeFont, Float aSize);

  /**
   * This creates a ScaledFont from TrueType data.
   *
   * aData - Pointer to the data
   * aSize - Size of the TrueType data
   * aFaceIndex - Index of the font face in the truetype data this ScaledFont needs to represent.
   * aGlyphSize - Size of the glyphs in this ScaledFont
   * aType - Type of ScaledFont that should be created.
   */
  static TemporaryRef<ScaledFont>
    CreateScaledFontForTrueTypeData(uint8_t *aData, uint32_t aSize, uint32_t aFaceIndex, Float aGlyphSize, FontType aType);

  /*
   * This creates a scaled font with an associated cairo_scaled_font_t, and
   * must be used when using the Cairo backend. The NativeFont and
   * cairo_scaled_font_t* parameters must correspond to the same font.
   */
  static TemporaryRef<ScaledFont>
    CreateScaledFontWithCairo(const NativeFont &aNativeFont, Float aSize, cairo_scaled_font_t* aScaledFont);

  /*
   * This creates a simple data source surface for a certain size. It allocates
   * new memory for the surface. This memory is freed when the surface is
   * destroyed.
   */
  static TemporaryRef<DataSourceSurface>
    CreateDataSourceSurface(const IntSize &aSize, SurfaceFormat aFormat);

  /*
   * This creates a simple data source surface for some existing data. It will
   * wrap this data and the data for this source surface. The caller is
   * responsible for deallocating the memory only after destruction of the
   * surface.
   */
  static TemporaryRef<DataSourceSurface>
    CreateWrappingDataSourceSurface(uint8_t *aData, int32_t aStride,
                                    const IntSize &aSize, SurfaceFormat aFormat);

  static TemporaryRef<DrawEventRecorder>
    CreateEventRecorderForFile(const char *aFilename);

  static void SetGlobalEventRecorder(DrawEventRecorder *aRecorder);

#ifdef USE_SKIA_GPU
  static TemporaryRef<DrawTarget>
    CreateDrawTargetSkiaWithGLContextAndGrGLInterface(GenericRefCountedBase* aGLContext,
                                                      GrGLInterface* aGrGLInterface,
                                                      const IntSize &aSize,
                                                      SurfaceFormat aFormat);

  static void
    SetGlobalSkiaCacheLimits(int aCount, int aSizeInBytes);
#endif

#if defined(USE_SKIA) && defined(MOZ_ENABLE_FREETYPE)
  static TemporaryRef<GlyphRenderingOptions>
    CreateCairoGlyphRenderingOptions(FontHinting aHinting, bool aAutoHinting);
#endif
  static TemporaryRef<DrawTarget>
    CreateDualDrawTarget(DrawTarget *targetA, DrawTarget *targetB);

#ifdef XP_MACOSX
  static TemporaryRef<DrawTarget> CreateDrawTargetForCairoCGContext(CGContextRef cg, const IntSize& aSize);
#endif

#ifdef WIN32
  static TemporaryRef<DrawTarget> CreateDrawTargetForD3D10Texture(ID3D10Texture2D *aTexture, SurfaceFormat aFormat);
  static TemporaryRef<DrawTarget>
    CreateDualDrawTargetForD3D10Textures(ID3D10Texture2D *aTextureA,
                                         ID3D10Texture2D *aTextureB,
                                         SurfaceFormat aFormat);

  static void SetDirect3D10Device(ID3D10Device1 *aDevice);
  static ID3D10Device1 *GetDirect3D10Device();
#ifdef USE_D2D1_1
  static void SetDirect3D11Device(ID3D11Device *aDevice);
  static ID3D11Device *GetDirect3D11Device();
  static ID2D1Device *GetD2D1Device();
#endif

  static TemporaryRef<GlyphRenderingOptions>
    CreateDWriteGlyphRenderingOptions(IDWriteRenderingParams *aParams);

  static uint64_t GetD2DVRAMUsageDrawTarget();
  static uint64_t GetD2DVRAMUsageSourceSurface();
  static void D2DCleanup();

private:
  static ID3D10Device1 *mD3D10Device;
#ifdef USE_D2D1_1
  static ID3D11Device *mD3D11Device;
  static ID2D1Device *mD2D1Device;
#endif
#endif

  static DrawEventRecorder *mRecorder;
};

}
}

#endif // _MOZILLA_GFX_2D_H
