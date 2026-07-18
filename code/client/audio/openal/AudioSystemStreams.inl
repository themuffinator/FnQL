// Included by AudioSystem.cpp inside its private implementation namespace.
// Music and raw PCM stream queueing.

class StreamPlayer {
public:
	void Init( OpenALDevice *device, ALuint source );
	void Shutdown();
	void Clear();
	void Update( float gain, bool allowStart = true );
	bool QueuePCM16( const short *samples, int frameCount, int channels, int sampleRate, bool startPlayback = true );
	bool Idle();
	int QueuedBufferCount() const { return queuedBuffers_; }
	double QueuedSeconds() const;

private:
	void ReclaimProcessedBuffers();

	OpenALDevice *device_ = nullptr;
	ALuint source_ = 0;
	int queuedBuffers_ = 0;
	std::deque<double> queuedDurations_;
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
			if ( !queuedDurations_.empty() ) {
				queuedDurations_.pop_front();
			}
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
	queuedDurations_.clear();
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

bool StreamPlayer::QueuePCM16( const short *samples, int frameCount, int channels, int sampleRate, bool startPlayback ) {
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
	queuedDurations_.push_back( static_cast<double>( frameCount ) / static_cast<double>( sampleRate ) );

	ALint state = 0;
	device_->AL().alGetError();
	device_->AL().alGetSourcei( source_, AL_SOURCE_STATE, &state );
	if ( device_->AL().alGetError() != AL_NO_ERROR ) {
		return true;
	}
	if ( startPlayback && state != AL_PLAYING ) {
		device_->AL().alSourcePlay( source_ );
	}

	return true;
}

void StreamPlayer::Update( float gain, bool allowStart ) {
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
	if ( allowStart && state != AL_PLAYING && queuedBuffers_ > 0 ) {
		device_->AL().alSourcePlay( source_ );
	}
}

bool StreamPlayer::Idle() {
	ReclaimProcessedBuffers();
	return queuedBuffers_ <= 0;
}

double StreamPlayer::QueuedSeconds() const {
	double seconds = 0.0;
	for ( const double duration : queuedDurations_ ) {
		seconds += duration;
	}
	return seconds;
}

class VoiceChatPlayer {
public:
	void Init( OpenALDevice *device );
	void Shutdown();
	void Clear();
	void Queue( int clientNum, int frameCount, int sampleRate, const short *samples );
	void Update( float gain );
	int LaneCount() const { return laneCount_; }
	int ActiveLaneCount() const;

private:
	struct Lane {
		fnql_audio_voice::LaneActivity activity;
		StreamPlayer player;
		ALuint source = 0;
		int startAtMs = 0;
	};

	static constexpr int kMaxQueuedBuffersPerLane = 20;
	static constexpr double kMaxQueuedSeconds =
		static_cast<double>( fnql_audio_voice::kLaneSampleCapacity ) / 22050.0;
	OpenALDevice *device_ = nullptr;
	std::array<Lane, fnql_audio_voice::kMaxLanes> lanes_{};
	int laneCount_ = 0;
};

void VoiceChatPlayer::Init( OpenALDevice *device ) {
	Shutdown();
	device_ = device;
	if ( device_ == nullptr ) {
		return;
	}

	// Keep a useful gameplay-source floor even on deliberately constrained
	// OpenAL configurations. Retail's five voice lanes are used whenever the
	// device budget permits it; fewer lanes degrade cleanly instead of starving
	// weapon, announcer, or ambient sounds.
	const int gameplayReserve = ( std::max )( 1, ( std::min )( 8, device_->TotalVoiceCount() / 2 ) );
	const int requestedLanes = ( std::min )( fnql_audio_voice::kMaxLanes,
		( std::max )( 0, device_->FreeVoiceCount() - gameplayReserve ) );
	for ( int i = 0; i < requestedLanes; ++i ) {
		const ALuint source = device_->AcquireVoiceSource();
		if ( source == 0 ) {
			break;
		}
		Lane &lane = lanes_[static_cast<size_t>( laneCount_++ )];
		lane.source = source;
		lane.player.Init( device_, source );
	}
	if ( laneCount_ < fnql_audio_voice::kMaxLanes ) {
		Com_DPrintf( "OpenAL remote voice: allocated %i of %i retail lanes to preserve gameplay sources\n",
			laneCount_, fnql_audio_voice::kMaxLanes );
	}
}

void VoiceChatPlayer::Shutdown() {
	for ( Lane &lane : lanes_ ) {
		lane.player.Shutdown();
		if ( device_ != nullptr && lane.source != 0 ) {
			device_->ReleaseVoiceSource( lane.source );
		}
		lane = Lane();
	}
	laneCount_ = 0;
	device_ = nullptr;
}

void VoiceChatPlayer::Clear() {
	for ( int i = 0; i < laneCount_; ++i ) {
		Lane &lane = lanes_[static_cast<size_t>( i )];
		lane.player.Clear();
		fnql_audio_voice::ResetLane( lane.activity );
		lane.startAtMs = 0;
	}
}

void VoiceChatPlayer::Queue( int clientNum, int frameCount, int sampleRate, const short *samples ) {
	if ( device_ == nullptr || laneCount_ <= 0 || samples == nullptr || frameCount <= 0 || sampleRate <= 0 ) {
		return;
	}

	std::array<fnql_audio_voice::LaneActivity, fnql_audio_voice::kMaxLanes> activity{};
	for ( int i = 0; i < laneCount_; ++i ) {
		Lane &lane = lanes_[static_cast<size_t>( i )];
		lane.activity.queued = !lane.player.Idle();
		activity[static_cast<size_t>( i )] = lane.activity;
	}

	const int now = Com_Milliseconds();
	const int laneIndex = fnql_audio_voice::SelectLane( activity.data(), laneCount_, clientNum, now );
	if ( laneIndex < 0 ) {
		Com_DPrintf( "OpenAL remote voice: all lanes are busy; dropping client %i packet\n", clientNum );
		return;
	}

	Lane &lane = lanes_[static_cast<size_t>( laneIndex )];
	if ( lane.activity.clientNum != clientNum ) {
		lane.player.Clear();
		fnql_audio_voice::ResetLane( lane.activity );
	}
	const double availableSeconds = kMaxQueuedSeconds - lane.player.QueuedSeconds();
	const int availableFrames = static_cast<int>( ( std::max )( 0.0, availableSeconds ) * sampleRate );
	const int queuedFrames = ( std::min )( frameCount, availableFrames );
	if ( lane.player.QueuedBufferCount() >= kMaxQueuedBuffersPerLane || queuedFrames <= 0 ) {
		Com_DPrintf( "OpenAL remote voice: client %i lane is full; dropping packet\n", clientNum );
		return;
	}

	const bool wasIdle = lane.player.QueuedBufferCount() <= 0;
	if ( !lane.player.QueuePCM16( samples, queuedFrames, 1, sampleRate, false ) ) {
		Com_DPrintf( "OpenAL remote voice: could not queue client %i packet\n", clientNum );
		return;
	}
	if ( queuedFrames < frameCount ) {
		Com_DPrintf( "OpenAL remote voice: client %i lane overflowed; truncated packet\n", clientNum );
	}
	if ( wasIdle ) {
		const float stepSeconds = ( s_voiceStep != nullptr ) ? ClampFloat( s_voiceStep->value, 0.0f, 0.25f ) : 0.0f;
		const std::uint32_t startAt = static_cast<std::uint32_t>( now ) +
			static_cast<std::uint32_t>( stepSeconds * 1000.0f );
		lane.startAtMs = static_cast<std::int32_t>( startAt );
	}
	lane.activity.clientNum = clientNum;
	lane.activity.queued = true;
	lane.activity.lastPacketMs = now;
}

void VoiceChatPlayer::Update( float gain ) {
	const int now = Com_Milliseconds();
	for ( int i = 0; i < laneCount_; ++i ) {
		Lane &lane = lanes_[static_cast<size_t>( i )];
		const bool allowStart = lane.player.QueuedBufferCount() >= 2 ||
			fnql_audio_voice::ElapsedMilliseconds( now, lane.startAtMs ) >= 0;
		lane.player.Update( gain, allowStart );
		lane.activity.queued = !lane.player.Idle();
	}
}

int VoiceChatPlayer::ActiveLaneCount() const {
	int active = 0;
	for ( int i = 0; i < laneCount_; ++i ) {
		if ( lanes_[static_cast<size_t>( i )].activity.queued ) {
			++active;
		}
	}
	return active;
}
