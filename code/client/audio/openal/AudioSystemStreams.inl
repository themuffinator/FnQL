// Included by AudioSystem.cpp inside its private implementation namespace.
// Music and raw PCM stream queueing.

class StreamPlayer {
public:
	void Init( OpenALDevice *device, ALuint source );
	void Shutdown();
	void Clear();
	void Update( float gain );
	bool QueuePCM16( const short *samples, int frameCount, int channels, int sampleRate );
	int QueuedBufferCount() const { return queuedBuffers_; }

private:
	void ReclaimProcessedBuffers();

	OpenALDevice *device_ = nullptr;
	ALuint source_ = 0;
	int queuedBuffers_ = 0;
};

void StreamPlayer::Init( OpenALDevice *device, ALuint source ) {
	device_ = device;
	source_ = source;
	Clear();
}

void StreamPlayer::ReclaimProcessedBuffers() {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	ALint processed = 0;
	device_->AL().alGetError();
	device_->AL().alGetSourcei( source_, AL_BUFFERS_PROCESSED, &processed );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		return;
	}
	while ( processed-- > 0 ) {
		ALuint buffer = 0;
		device_->AL().alSourceUnqueueBuffers( source_, 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR ) {
			break;
		}
		if ( buffer != 0 ) {
			device_->BufferPool().Release( buffer );
			queuedBuffers_ = ( std::max )( 0, queuedBuffers_ - 1 );
		}
	}
}

void StreamPlayer::Clear() {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	device_->AL().alSourceStop( source_ );
	ReclaimProcessedBuffers();

	ALint queued = 0;
	device_->AL().alGetError();
	device_->AL().alGetSourcei( source_, AL_BUFFERS_QUEUED, &queued );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		queuedBuffers_ = 0;
		return;
	}
	while ( queued-- > 0 ) {
		ALuint buffer = 0;
		device_->AL().alGetError();
		device_->AL().alSourceUnqueueBuffers( source_, 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR ) {
			break;
		}
		if ( buffer != 0 ) {
			device_->BufferPool().Release( buffer );
		}
	}
	queuedBuffers_ = 0;
	device_->AL().alSourcei( source_, AL_BUFFER, 0 );
	device_->SetSourceSpatialize( source_, false );
	device_->SetSourceDirectChannels( source_, true );
	device_->AL().alSourcei( source_, AL_SOURCE_RELATIVE, AL_TRUE );
	device_->ConfigureSourceDistance( source_, false );
	device_->AL().alSource3f( source_, AL_POSITION, 0.0f, 0.0f, -1.0f );
	device_->AL().alSource3f( source_, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	device_->AL().alSourcef( source_, AL_GAIN, 1.0f );
}

void StreamPlayer::Shutdown() {
	Clear();
	device_ = nullptr;
	source_ = 0;
}

bool StreamPlayer::QueuePCM16( const short *samples, int frameCount, int channels, int sampleRate ) {
	if ( device_ == nullptr || source_ == 0 || samples == nullptr || frameCount <= 0 ||
		!device_->SupportsPCMChannels( channels ) || sampleRate <= 0 ) {
		return false;
	}
	if ( frameCount > std::numeric_limits<int>::max() / channels ||
		!PCMByteCountFitsALsizei( static_cast<size_t>( frameCount ) * static_cast<size_t>( channels ) ) ) {
		return false;
	}

	const ALenum format = device_->PCM16FormatForChannels( channels );
	if ( format == 0 ) {
		return false;
	}

	ReclaimProcessedBuffers();

	ALuint buffer = device_->BufferPool().Acquire();
	if ( buffer == 0 ) {
		return false;
	}

	const int sampleCount = frameCount * channels;
	const ALsizei byteCount = static_cast<ALsizei>( static_cast<size_t>( sampleCount ) * sizeof( short ) );
	device_->AL().alGetError();
	device_->AL().alBufferData( buffer, format, samples, byteCount, sampleRate );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		device_->BufferPool().Release( buffer );
		return false;
	}

	device_->AL().alGetError();
	device_->AL().alSourceQueueBuffers( source_, 1, &buffer );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		device_->BufferPool().Release( buffer );
		return false;
	}

	++queuedBuffers_;

	ALint state = 0;
	device_->AL().alGetError();
	device_->AL().alGetSourcei( source_, AL_SOURCE_STATE, &state );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		return true;
	}
	if ( state != AL_PLAYING ) {
		device_->AL().alSourcePlay( source_ );
	}

	return true;
}

void StreamPlayer::Update( float gain ) {
	if ( device_ == nullptr || source_ == 0 ) {
		return;
	}

	device_->AL().alSourcef( source_, AL_GAIN, gain );
	ReclaimProcessedBuffers();

	ALint state = 0;
	device_->AL().alGetError();
	device_->AL().alGetSourcei( source_, AL_SOURCE_STATE, &state );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		return;
	}
	if ( state != AL_PLAYING && queuedBuffers_ > 0 ) {
		device_->AL().alSourcePlay( source_ );
	}
}
