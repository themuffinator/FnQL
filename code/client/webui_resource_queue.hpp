/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/
#ifndef FNQL_CLIENT_WEBUI_RESOURCE_QUEUE_HPP
#define FNQL_CLIENT_WEBUI_RESOURCE_QUEUE_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace fnql::webui {

// Client-main-thread queue for asynchronous resources whose provider can
// report only ready/not-ready. Retain unavailable requests, retry them by
// elapsed time, and let provider events wake a shared path immediately.
class ResourceRetryQueue final {
public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;
	inline static constexpr std::size_t kCapacity = 64;
	inline static constexpr std::size_t kPathCapacity = 4096;

	struct Batch {
		std::array<char, kPathCapacity> path{};
		std::size_t pathLength = 0;
		std::array<int, kCapacity> requestIds{};
		std::size_t count = 0;
		// A callback may enqueue a replacement using an old request ID while
		// the provider fetch is in progress. Completion validates generations
		// as well as IDs and paths before producing the response recipient list.
		std::array<std::uint64_t, kCapacity> generations{};

		std::string_view Path() const noexcept {
			return { path.data(), pathLength < path.size() ? pathLength : 0 };
		}
	};

	bool Enqueue( int requestId, std::string_view path, TimePoint now ) noexcept {
		if ( !ValidPath( path ) ) {
			return false;
		}
		std::size_t destination = kCapacity;
		std::size_t shared = kCapacity;
		for ( std::size_t index = 0; index < entries_.size(); ++index ) {
			const Entry &entry = entries_[index];
			if ( entry.active ) {
				if ( entry.requestId == requestId ) {
					destination = index;
				}
				if ( SamePath( entry.Path(), path ) ) {
					shared = index;
				}
			}
		}
		if ( destination == kCapacity ) {
			for ( std::size_t index = 0; index < entries_.size(); ++index ) {
				if ( !entries_[index].active ) {
					destination = index;
					break;
				}
			}
		}
		if ( destination == kCapacity ) {
			return false;
		}

		// Duplicate URLs and replacement IDs retain a path's current retry
		// deadline. Repeated browser requests cannot defeat the backoff.
		const TimePoint due = shared < kCapacity ? entries_[shared].due : now;
		const unsigned int delay = shared < kCapacity ? entries_[shared].delayMilliseconds : 250;
		Entry &entry = entries_[destination];
		if ( !entry.active ) {
			++activeCount_;
		}
		entry.requestId = requestId;
		entry.generation = ++nextGeneration_;
		entry.active = true;
		entry.due = due;
		entry.delayMilliseconds = delay;
		entry.pathLength = path.size();
		std::memmove( entry.path.data(), path.data(), path.size() );
		entry.path[path.size()] = '\0';
		return true;
	}

	void Wake( std::string_view path, TimePoint now ) noexcept {
		for ( Entry &entry : entries_ ) {
			if ( entry.active && ( path.empty() || SamePath( entry.Path(), path ) ) ) {
				entry.due = now;
				entry.delayMilliseconds = 250;
			}
		}
	}

	bool NextDue( TimePoint now, Batch &batch ) noexcept {
		batch.count = 0;
		batch.pathLength = 0;
		batch.path[0] = '\0';
		if ( Empty() ) {
			return false;
		}
		for ( std::size_t offset = 0; offset < entries_.size(); ++offset ) {
			const std::size_t index = ( cursor_ + offset ) % entries_.size();
			const Entry &candidate = entries_[index];
			if ( !candidate.active || candidate.due > now ) {
				continue;
			}
			batch.pathLength = candidate.pathLength;
			std::memcpy( batch.path.data(), candidate.path.data(), candidate.pathLength + 1 );
			const auto delay = std::chrono::milliseconds( candidate.delayMilliseconds );
			const TimePoint next = now > TimePoint::max() - delay ? TimePoint::max() : now + delay;
			const unsigned int nextDelay = candidate.delayMilliseconds < 2000
				? candidate.delayMilliseconds * 2 : 2000;
			for ( Entry &entry : entries_ ) {
				if ( entry.active && SamePath( entry.Path(), batch.Path() ) ) {
					batch.requestIds[batch.count] = entry.requestId;
					batch.generations[batch.count++] = entry.generation;
					// Schedule failure before returning to provider callbacks. An
					// event delivered by that work can safely override this delay.
					entry.due = next;
					entry.delayMilliseconds = nextDelay;
				}
			}
			cursor_ = ( index + 1 ) % entries_.size();
			return true;
		}
		return false;
	}

	// Compact the batch to recipients still owned by this queue, removing
	// those entries before the caller sends any responses that may reenter it.
	void Complete( Batch &batch ) noexcept {
		std::size_t completed = 0;
		const std::size_t count = batch.count < kCapacity ? batch.count : kCapacity;
		for ( std::size_t index = 0; index < count; ++index ) {
			for ( Entry &entry : entries_ ) {
				if ( entry.active && entry.requestId == batch.requestIds[index]
					&& entry.generation == batch.generations[index]
					&& SamePath( entry.Path(), batch.Path() ) ) {
					batch.requestIds[completed] = entry.requestId;
					batch.generations[completed++] = entry.generation;
					entry.active = false;
					--activeCount_;
					break;
				}
			}
		}
		batch.count = completed;
	}

	std::size_t TakeAll( std::array<int, kCapacity> &requestIds ) noexcept {
		std::size_t count = 0;
		for ( const Entry &entry : entries_ ) {
			if ( entry.active ) {
				requestIds[count++] = entry.requestId;
			}
		}
		Clear();
		return count;
	}

	void Clear() noexcept {
		for ( Entry &entry : entries_ ) {
			entry.active = false;
			entry.requestId = 0;
			entry.generation = 0;
			entry.pathLength = 0;
			entry.path[0] = '\0';
			entry.due = {};
			entry.delayMilliseconds = 250;
		}
		cursor_ = 0;
		activeCount_ = 0;
		// Preserve the generation sequence: an old in-flight batch must not
		// match a new request with the same ID after navigation clears the queue.
	}

	bool Empty() const noexcept {
		return activeCount_ == 0;
	}

	std::size_t Size() const noexcept {
		return activeCount_;
	}

private:
	struct Entry {
		std::array<char, kPathCapacity> path{};
		std::size_t pathLength = 0;
		int requestId = 0;
		std::uint64_t generation = 0;
		TimePoint due{};
		unsigned int delayMilliseconds = 250;
		bool active = false;

		std::string_view Path() const noexcept {
			return { path.data(), pathLength };
		}
	};

	static std::string_view PathKey( std::string_view path ) noexcept {
		return path.substr( 0, path.find_first_of( "?#" ) );
	}

	static bool ValidPath( std::string_view path ) noexcept {
		if ( path.size() >= kPathCapacity || PathKey( path ).empty() ) {
			return false;
		}
		for ( const unsigned char character : path ) {
			if ( character < 0x20 || character == 0x7f ) {
				return false;
			}
		}
		return true;
	}

	static bool SamePath( std::string_view left, std::string_view right ) noexcept {
		left = PathKey( left );
		right = PathKey( right );
		if ( left.size() != right.size() || left.empty() ) {
			return false;
		}
		for ( std::size_t index = 0; index < left.size(); ++index ) {
			const auto fold = []( unsigned char value ) {
				return value >= 'A' && value <= 'Z'
					? static_cast<unsigned char>( value + ( 'a' - 'A' ) ) : value;
			};
			if ( fold( static_cast<unsigned char>( left[index] ) )
				!= fold( static_cast<unsigned char>( right[index] ) ) ) {
				return false;
			}
		}
		return true;
	}

	std::array<Entry, kCapacity> entries_{};
	std::size_t cursor_ = 0;
	std::size_t activeCount_ = 0;
	std::uint64_t nextGeneration_ = 0;
};

} // namespace fnql::webui

#endif // FNQL_CLIENT_WEBUI_RESOURCE_QUEUE_HPP
