#include "Mockups.h"

#include "Helpers.h"

#include <common/serialization.h>

#include <stdexcept>
#include <strmd/strmd/deserializer.h>
#include <ut/assert.h>

using wpl::mt::thread;
using namespace std;
using namespace std::placeholders;

namespace micro_profiler
{
	namespace tests
	{
		namespace mockups
		{
			namespace
			{
				class buffer_reader
				{
				public:
					buffer_reader(const void *message, size_t size)
						: _ptr(static_cast<const unsigned char *>(message)), _remaining(size)
					{	}

					void read(void *data, size_t size)
					{
						assert_is_true(size <= _remaining);
						memcpy(data, _ptr, size);
						_ptr += size;
						_remaining -= size;
					}

				private:
					const unsigned char *_ptr;
					size_t _remaining;
				};
			}

			class Frontend
			{
			public:
				explicit Frontend(FrontendState &state);
				Frontend(const Frontend &other);
				~Frontend();

				void operator ()(const void *buffer, size_t size);

				static Frontend Create(FrontendState& state);

			private:
				const Frontend &operator =(const Frontend &rhs);

			private:
				FrontendState &_state;
			};

			frontend_factory FrontendState::MakeFactory()
			{	return bind(&Frontend::Create, ref(*this));	}

			Frontend::Frontend(FrontendState &state)
				: _state(state)
			{	++_state.ref_count;	}

			Frontend::Frontend(const Frontend &other)
				: _state(other._state)
			{	++_state.ref_count;	}

			Frontend::~Frontend()
			{	--_state.ref_count;	}

			Frontend Frontend::Create(FrontendState& state)
			{	return Frontend(state);	}

			template <typename ArchiveT>
			void serialize(ArchiveT &a, FrontendState &state)
			{
				commands c;

				a(c);
				state.update_log.push_back(FrontendState::ReceivedEntry());
				FrontendState::ReceivedEntry &e = state.update_log.back();
				switch (c)
				{
				case init:
					state.update_log.pop_back();
					a(state.process_init);
					if (state.oninitialized)
						state.oninitialized();
					break;

				case modules_loaded:
					a(e.image_loads);
					for (loaded_modules::iterator i = e.image_loads.begin(); i != e.image_loads.end(); ++i)
						toupper(i->second);				
					state.modules_state_updated.raise();
					break;

				case update_statistics:
					a(e.update);
					state.updated.raise();
					state.update_lock.wait();
					break;

				case modules_unloaded:
					a(e.image_unloads);
					state.modules_state_updated.raise();
				}
			}

			void Frontend::operator ()(const void *message, size_t size)
			{
				buffer_reader reader(message, size);
				strmd::deserializer<buffer_reader> a(reader);

				a(_state);
			}


			FrontendState::FrontendState(const function<void()>& oninitialized_)
				: update_lock(true, false), oninitialized(oninitialized_), updated(false, true),
					modules_state_updated(false, true), ref_count(0)
			{	}


			Tracer::Tracer(timestamp_t latency)
				: _latency(latency)
			{	}

			void Tracer::read_collected(acceptor &a)
			{
				scoped_lock l(_mutex);

				for (TracesMap::const_iterator i = _traces.begin(); i != _traces.end(); ++i)
				{
					a.accept_calls(i->first, &i->second[0], i->second.size());
				}
				_traces.clear();
			}

			timestamp_t Tracer::profiler_latency() const throw()
			{	return _latency;	}
		}
	}
}
