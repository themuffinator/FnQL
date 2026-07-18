#pragma once

#include <cstdint>

namespace fnql::client {

struct CanvasGeometry {
	float scale = 1.0f;
	float biasX = 0.0f;
	float biasY = 0.0f;
};

// Fit the retail 640x480 virtual canvas inside the active render surface while
// retaining its aspect ratio. The unused axis is centred so input and drawing
// share the same origin after every drawable-size change.
inline CanvasGeometry CalculateCanvasGeometry( int width, int height ) {
	CanvasGeometry geometry;

	if ( width <= 0 || height <= 0 ) {
		return geometry;
	}

	if ( static_cast<std::int64_t>( width ) * 480 >
		static_cast<std::int64_t>( height ) * 640 ) {
		geometry.scale = height * ( 1.0f / 480.0f );
		geometry.biasX = 0.5f * ( width - height * ( 640.0f / 480.0f ) );
	} else {
		geometry.scale = width * ( 1.0f / 640.0f );
		geometry.biasY = 0.5f * ( height - width * ( 480.0f / 640.0f ) );
	}

	return geometry;
}

} // namespace fnql::client
