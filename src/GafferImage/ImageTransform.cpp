//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013-2015, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of Image Engine Design nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "IECore/AngleConversion.h"

#include "Gaffer/Context.h"

#include "GafferImage/ImageTransform.h"
#include "GafferImage/Reformat.h"
#include "GafferImage/Filter.h"
#include "GafferImage/FormatPlug.h"
#include "GafferImage/ImagePlug.h"
#include "GafferImage/Sampler.h"

using namespace Gaffer;
using namespace IECore;
using namespace GafferImage;

IE_CORE_DEFINERUNTIMETYPED( ImageTransform );

//////////////////////////////////////////////////////////////////////////
// Internal implementation details
//////////////////////////////////////////////////////////////////////////

namespace GafferImage
{

namespace Detail
{

class Implementation : public ImageProcessor
{
	public :

		Implementation( const std::string &name=staticTypeName() );

		virtual ~Implementation(){};

		IE_CORE_DECLARERUNTIMETYPEDEXTENSION( Implementation, ImageTransformImplementationTypeId, ImageProcessor );

		virtual void hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const;
		virtual void hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const;
		virtual void hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const;

		virtual GafferImage::Format computeFormat( const Gaffer::Context *context, const ImagePlug *parent ) const;
		virtual Imath::Box2i computeDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const;
		virtual IECore::ConstFloatVectorDataPtr computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const;

		/// Plug accessors.
		Gaffer::Transform2DPlug *transformPlug();
		const Gaffer::Transform2DPlug *transformPlug() const;
		GafferImage::FilterPlug *filterPlug();
		const GafferImage::FilterPlug *filterPlug() const;
		GafferImage::FormatPlug *outputFormatPlug();
		const GafferImage::FormatPlug *outputFormatPlug() const;

		virtual void affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const;
		virtual bool enabled() const;

	private :

		static size_t g_firstPlugIndex;

		// A useful method that returns an axis-aligned box that contains box*m.
		Imath::Box2i transformBox( const Imath::M33f &m, const Imath::Box2i &box ) const;

		// A method that uses the input and output format along with the transform plug to create
		// the transform matrix that will compensate for the reformat node having resized the input.
		Imath::M33f computeAdjustedMatrix() const;
};

size_t Implementation::g_firstPlugIndex = 0;

Implementation::Implementation( const std::string &name )
	:	ImageProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new Gaffer::Transform2DPlug( "transform" ) );
	addChild( new GafferImage::FilterPlug( "filter" ) );
	addChild( new GafferImage::FormatPlug( "outputFormat" ) );

	// We don't ever want to change these, so we make pass-through connections.
	outPlug()->metadataPlug()->setInput( inPlug()->metadataPlug() );
	outPlug()->channelNamesPlug()->setInput( inPlug()->channelNamesPlug() );
}

Gaffer::Transform2DPlug *Implementation::transformPlug()
{
	return getChild<Gaffer::Transform2DPlug>( g_firstPlugIndex );
}

const Gaffer::Transform2DPlug *Implementation::transformPlug() const
{
	return getChild<Gaffer::Transform2DPlug>( g_firstPlugIndex );
}

GafferImage::FilterPlug *Implementation::filterPlug()
{
	return getChild<GafferImage::FilterPlug>( g_firstPlugIndex+1 );
}

const GafferImage::FilterPlug *Implementation::filterPlug() const
{
	return getChild<GafferImage::FilterPlug>( g_firstPlugIndex+1 );
}

GafferImage::FormatPlug *Implementation::outputFormatPlug()
{
	return getChild<GafferImage::FormatPlug>( g_firstPlugIndex + 2 );
}

const GafferImage::FormatPlug *Implementation::outputFormatPlug() const
{
	return getChild<GafferImage::FormatPlug>( g_firstPlugIndex + 2 );
}

void Implementation::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageProcessor::affects( input, outputs );

	if( input == inPlug()->formatPlug()	)
	{
		outputs.push_back( outPlug()->channelDataPlug() );
	}
	else if( input == inPlug()->dataWindowPlug() )
	{
		outputs.push_back( outPlug()->dataWindowPlug() );
	}
	else if( input == inPlug()->channelDataPlug() )
	{
		outputs.push_back( outPlug()->channelDataPlug() );
	}
	else if ( input == outputFormatPlug() )
	{
		outputs.push_back( outPlug()->formatPlug() );
	}
	else if ( transformPlug()->isAncestorOf( input ) )
	{
		outputs.push_back( outPlug()->dataWindowPlug() );
		outputs.push_back( outPlug()->channelDataPlug() );
	}
	else if ( input == filterPlug() )
	{
		outputs.push_back( outPlug()->channelDataPlug() );
	}
}

bool Implementation::enabled() const
{
	if ( !ImageProcessor::enabled() )
	{
		return false;
	}

	// Disable the node if it isn't doing anything...
	Imath::V2f scale = transformPlug()->scalePlug()->getValue();
	Imath::V2f translate = transformPlug()->translatePlug()->getValue();
	float rotate = transformPlug()->rotatePlug()->getValue();
	if (
		rotate == 0 &&
		scale.x == 1 && scale.y == 1 &&
		translate.x == 0 && translate.y == 0
		)
	{
		return false;
	}

	return true;
}

void Implementation::hashFormat( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	h = outputFormatPlug()->hash();
}

GafferImage::Format Implementation::computeFormat( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	return outputFormatPlug()->getValue();
}

void Implementation::hashDataWindow( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashDataWindow( output, context, h );
	inPlug()->dataWindowPlug()->hash( h );
	transformPlug()->hash( h );
}

Imath::Box2i Implementation::computeDataWindow( const Gaffer::Context *context, const ImagePlug *parent ) const
{
	Imath::Box2i inWindow( inPlug()->dataWindowPlug()->getValue() );
	Imath::M33f t = computeAdjustedMatrix();
	Imath::Box2i outWindow( transformBox( t, inWindow ) );
	return outWindow;
}

void Implementation::hashChannelData( const GafferImage::ImagePlug *output, const Gaffer::Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hashChannelData( output, context, h );

	// Hash all of the tiles that the sample requires for this tile.
	Imath::V2i tileOrigin( Context::current()->get<Imath::V2i>( ImagePlug::tileOriginContextName ) );
	std::string channelName( Context::current()->get<std::string>( ImagePlug::channelNameContextName ) );
	Imath::M33f sampleTransform( computeAdjustedMatrix().inverse() );
	Imath::Box2i tile( transformBox( sampleTransform, Imath::Box2i( tileOrigin, tileOrigin + Imath::V2i( ImagePlug::tileSize() ) ) ) );

	GafferImage::FilterPtr filter = GafferImage::Filter::create( filterPlug()->getValue() );
	Sampler sampler( inPlug(), channelName, tile, filter );
	sampler.hash( h );

	// Hash in the origin of the output tile. Multiple output tiles may share the exact same set of input
	// tiles, but will reference different parts of them depending on the tile origin.
	h.append( tileOrigin );

	// Hash the filter type that we are using.
	filterPlug()->hash( h );

	// Finally we hash the transformation.
	transformPlug()->hash( h );
}

IECore::ConstFloatVectorDataPtr Implementation::computeChannelData( const std::string &channelName, const Imath::V2i &tileOrigin, const Gaffer::Context *context, const ImagePlug *parent ) const
{
	// Allocate the new tile
	FloatVectorDataPtr outDataPtr = new FloatVectorData;
	std::vector<float> &out = outDataPtr->writable();
	out.resize( ImagePlug::tileSize() * ImagePlug::tileSize() );

	// Work out the bounds of the tile that we are outputting to.
	Imath::Box2i tile( tileOrigin, Imath::V2i( tileOrigin.x + ImagePlug::tileSize(), tileOrigin.y + ImagePlug::tileSize() ) );

	// Work out the sample area that we require to compute this tile.

	Imath::M33f t = computeAdjustedMatrix().inverse();
	Imath::Box2i sampleBox( transformBox( t, tile ) );

	GafferImage::FilterPtr filter = GafferImage::Filter::create( filterPlug()->getValue() );
	Sampler sampler( inPlug(), channelName, sampleBox, filter );
	for ( int j = 0; j < ImagePlug::tileSize(); ++j )
	{
		for ( int i = 0; i < ImagePlug::tileSize(); ++i )
		{
			Imath::V3f p( i+tile.min.x+.5, j+tile.min.y+.5, 1. );
			p *= t;
			out[ i + j*ImagePlug::tileSize() ] = sampler.sample( p.x, p.y );
		}
	}

	return outDataPtr;
}

Imath::M33f Implementation::computeAdjustedMatrix() const
{
	Format inFormat = inPlug()->formatPlug()->getValue();
	Format outFormat = outputFormatPlug()->getValue();

	// The desired scale factor.
	Imath::V2f scale = transformPlug()->scalePlug()->getValue();

	// The actual scale factor that the reformat node has resized the input to.
	Imath::V2f trueScale = Imath::V2f(
		float( inFormat.getDisplayWindow().size().x ) / ( outFormat.getDisplayWindow().size().x ),
		float( inFormat.getDisplayWindow().size().y ) / ( outFormat.getDisplayWindow().size().y )
	);

	// To transform the image correctly we need to first move the image to the pivot point.
	Imath::M33f pi;
	Imath::V2f invPivotVec = -transformPlug()->pivotPlug()->getValue();
	invPivotVec *= trueScale;
	pi.translate( invPivotVec );

	// As the input was only scaled to the nearest integer bounding box by the reformat node we need to
	// do a slight scale adjustment to achieve any sub-pixel scale factor.
	Imath::V2f scaleOffset = scale / trueScale;
	Imath::M33f s;
	s.scale( scaleOffset );

	// The rotation component.
	Imath::M33f r;
	r.rotate( -IECore::degreesToRadians( transformPlug()->rotatePlug()->getValue() ) );

	// The translation component.
	Imath::M33f t;
	t.translate( transformPlug()->translatePlug()->getValue() );

	// Here we invert the pivot vector and translate the image back.
	Imath::M33f p;
	p.translate( transformPlug()->pivotPlug()->getValue() );

	// Concatenate the transforms.
	Imath::M33f result = pi * s * r * t * p;

	return result;
}

Imath::Box2i Implementation::transformBox( const Imath::M33f &m, const Imath::Box2i &box ) const
{
	Imath::V3f pt[4];
	pt[0] = Imath::V3f( box.min.x, box.min.y, 1. );
	pt[1] = Imath::V3f( box.max.x-1, box.max.y-1, 1. );
	pt[2] = Imath::V3f( box.max.x-1, box.min.y, 1. );
	pt[3] = Imath::V3f( box.min.x, box.max.y-1, 1. );

	int maxX = std::numeric_limits<int>::min();
	int maxY = std::numeric_limits<int>::min();
	int minX = std::numeric_limits<int>::max();
	int minY = std::numeric_limits<int>::max();

	for( unsigned int i = 0; i < 4; ++i )
	{
		pt[i] = pt[i] * m;
		maxX = std::max( int( ceil( pt[i].x ) ), maxX );
		maxY = std::max( int( ceil( pt[i].y ) ), maxY );
		minX = std::min( int( floor( pt[i].x ) ), minX );
		minY = std::min( int( floor( pt[i].y ) ), minY );
	}

	return Imath::Box2i( Imath::V2i( minX, minY ), Imath::V2i( maxX, maxY ) + Imath::V2i( 1 ) );
}

} // namespace Detail

} // namespace GafferImage

//////////////////////////////////////////////////////////////////////////
// Implementation of ImageTransform
//////////////////////////////////////////////////////////////////////////

size_t ImageTransform::g_firstPlugIndex = 0;

ImageTransform::ImageTransform( const std::string &name )
	:	ImageProcessor( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new Gaffer::Transform2DPlug( "transform" ) );

	FilterPlug *filterPlug = new FilterPlug( "filter" );
	addChild( filterPlug );

	addChild( new GafferImage::FormatPlug( "__scaledFormat", Gaffer::Plug::Out ) );

	// Create the internal implementation of our transform and connect it up the our plugs.
	GafferImage::Reformat *r = new GafferImage::Reformat( "__reformat" );
	r->inPlug()->setInput( inPlug() );
	r->filterPlug()->setInput( filterPlug );
	r->formatPlug()->setInput( formatPlug() );
	r->enabledPlug()->setInput( enabledPlug() );
	addChild( r );

	Detail::Implementation *t = new Detail::Implementation( "__implementation" );
	t->inPlug()->setInput( r->outPlug() );
	t->transformPlug()->setInput( transformPlug() );
	t->outputFormatPlug()->setInput( inPlug()->formatPlug() );
	t->filterPlug()->setInput( filterPlug );
	t->enabledPlug()->setInput( enabledPlug() );
	addChild( t );

	outPlug()->setInput( t->outPlug() );
}

bool ImageTransform::enabled() const
{
	if ( !ImageProcessor::enabled() )
	{
		return false;
	}

	// Disable the node if it isn't doing anything...
	Imath::V2f scale = transformPlug()->scalePlug()->getValue();
	Imath::V2f translate = transformPlug()->translatePlug()->getValue();
	float rotate = transformPlug()->rotatePlug()->getValue();
	if (
		rotate == 0 &&
		scale.x == 1 && scale.y == 1 &&
		translate.x == 0 && translate.y == 0
		)
	{
		return false;
	}

	return true;
}

Gaffer::Transform2DPlug *ImageTransform::transformPlug()
{
	return getChild<Gaffer::Transform2DPlug>( g_firstPlugIndex );
}

const Gaffer::Transform2DPlug *ImageTransform::transformPlug() const
{
	return getChild<Gaffer::Transform2DPlug>( g_firstPlugIndex );
}

GafferImage::FormatPlug *ImageTransform::formatPlug()
{
	return getChild<GafferImage::FormatPlug>( g_firstPlugIndex + 2 );
}

const GafferImage::FormatPlug *ImageTransform::formatPlug() const
{
	return getChild<GafferImage::FormatPlug>( g_firstPlugIndex + 2 );
}

void ImageTransform::affects( const Gaffer::Plug *input, AffectedPlugsContainer &outputs ) const
{
	ImageProcessor::affects( input, outputs );

	if( transformPlug()->scalePlug()->isAncestorOf( input ) ||
		input == inPlug()->formatPlug()
	)
	{
		outputs.push_back( formatPlug() );
	}
}

void ImageTransform::hash( const ValuePlug *output, const Context *context, IECore::MurmurHash &h ) const
{
	ImageProcessor::hash( output, context, h );

	const FormatPlug *fPlug = IECore::runTimeCast<const FormatPlug>(output);
	if( fPlug == formatPlug() )
	{
		h = inPlug()->formatPlug()->hash();
		transformPlug()->scalePlug()->hash( h );
		return;
	}
}

void ImageTransform::compute( ValuePlug *output, const Context *context ) const
{
	if( output == formatPlug() )
	{
		Imath::V2f scale = transformPlug()->scalePlug()->getValue();
		GafferImage::Format f = inPlug()->formatPlug()->getValue();

		Imath::Box2i newDisplayWindow(
			Imath::V2i( IECore::fastFloatFloor( f.getDisplayWindow().min.x * scale.x ), IECore::fastFloatFloor( f.getDisplayWindow().min.y * scale.y ) ),
			Imath::V2i( IECore::fastFloatCeil( ( f.getDisplayWindow().max.x - 1 ) * scale.x ) + 1, IECore::fastFloatCeil( ( f.getDisplayWindow().max.y - 1 ) * scale.y ) + 1 )
		);

		static_cast<FormatPlug *>( output )->setValue( GafferImage::Format( newDisplayWindow, 1.f ) );
		return;
	}

	ImageProcessor::compute( output, context );
}

ImageTransform::~ImageTransform()
{
}
