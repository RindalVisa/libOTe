#pragma once
#include <libOTe/config.h>
#ifdef ENABLE_SOFTSPOKEN_OT

#include <boost/optional.hpp>
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Network/Channel.h>
#include "libOTe/TwoChooseOne/OTExtInterface.h"
#include "libOTe/Tools/Chunker.h"
#include "libOTe/Tools/ReplicationCode.h"
#include "libOTe/Tools/Tools.h"
#include "libOTe/Vole/SoftSpokenOT/SmallFieldVole.h"
#include "libOTe/Vole/SoftSpokenOT/SubspaceVole.h"

namespace osuCrypto
{
namespace SoftSpokenOT
{

// Builds a Delta OT out of SubspaceVole.

class DotSemiHonestSender :
	public OtExtSender,
	public TimerAdapter,
	private ChunkedReceiver<DotSemiHonestSender, std::tuple<std::array<block, 2>>>
{
public:
	// Present once base OTs have finished.
	boost::optional<SubspaceVoleReceiver<ReplicationCode>> vole;

	size_t fieldBitsThenBlockIdx; // fieldBits before initialization, blockIdx after.
	size_t numThreads;

	DotSemiHonestSender(size_t fieldBits, size_t numThreads_ = 1) :
		ChunkerBase(this),
		fieldBitsThenBlockIdx(fieldBits),
		numThreads(numThreads_)
	{
		if (fieldBits == 0)
			throw std::invalid_argument("There is no field with cardinality 2^0 = 1.");
	}

	size_t fieldBits() const
	{
		return vole ? vole->vole.fieldBits : fieldBitsThenBlockIdx;
	}

	size_t wSize() const { return vole->wSize(); }
	size_t wPadded() const { return vole->wPadded(); }

	block delta() const
	{
		block d;
		memcpy(&d, vole.value().vole.delta.data(), sizeof(block));
		return d;
	}

	u64 baseOtCount() const override final
	{
		// Can only use base OTs in groups of fieldBits.
		return roundUpTo(gOtExtBaseOtCount, fieldBits());
	}

	bool hasBaseOts() const override final
	{
		return vole.has_value();
	}

	DotSemiHonestSender splitBase()
	{
		throw RTE_LOC; // TODO: unimplemented.
	}

	std::unique_ptr<OtExtSender> split() override
	{
		return std::make_unique<DotSemiHonestSender>(splitBase());
	}

	void setBaseOts(
		span<block> baseRecvOts,
		const BitVector& choices,
		PRNG& prng,
		Channel& chl) override;

	virtual void initTemporaryStorage() { ChunkerBase::initTemporaryStorage(); }

	void send(span<std::array<block, 2>> messages, PRNG& prng, Channel& chl) override;

	// Low level functions.

	// Perform 128 random VOLEs (assuming that the messages have been received from the receiver),
	// and output the msg_0s. msg_1 will be msg_0 ^ delta. The output is not bitsliced, i.e. it is
	// transposed from what the SubspaceVole outputs. outW must have length wPadded() (which may be
	// greater than 128). The extra blocks are treated as padding and may be overwritten, either
	// with unneeded extra VOLE bits or padding from the VOLE.
	void generateRandom(size_t blockIdx, span<block> outW)
	{
		vole->generateRandom(blockIdx, outW);
		transpose128(outW.data());
	}

	void generateChosen(size_t blockIdx, span<block> outW)
	{
		vole->generateChosen(blockIdx, outW);
		transpose128(outW.data());
	}

protected:
	using ChunkerBase = ChunkedReceiver<DotSemiHonestSender, std::tuple<std::array<block, 2>>>;
	friend ChunkerBase;
	friend ChunkerBase::Base;

	static const size_t chunkSize = 128;
	static const size_t commSize = commStepSize * superBlkSize; // picked to match the other OTs.
	size_t paddingSize() const { return std::max(wPadded(), 2 * chunkSize) - 2 * chunkSize; }

	void recvBuffer(Channel& chl, size_t batchSize) { vole->recv(chl, 0, batchSize); }
	TRY_FORCEINLINE void processChunk(size_t numUsed, span<std::array<block, 2>> messages);
};

class DotSemiHonestReceiver :
	public OtExtReceiver,
	public TimerAdapter,
	private ChunkedSender<DotSemiHonestReceiver, std::tuple<block>>
{
public:
	// Present once base OTs have finished.
	boost::optional<SubspaceVoleSender<ReplicationCode>> vole;

	size_t fieldBitsThenBlockIdx; // fieldBits before initialization, blockIdx after.
	size_t numThreads;

	DotSemiHonestReceiver(size_t fieldBits, size_t numThreads_ = 1) :
		ChunkerBase(this),
		fieldBitsThenBlockIdx(fieldBits),
		numThreads(numThreads_)
	{
		if (fieldBits == 0)
			throw std::invalid_argument("There is no field with cardinality 2^0 = 1.");
	}

	size_t fieldBits() const
	{
		return vole ? vole->vole.fieldBits : fieldBitsThenBlockIdx;
	}

	size_t vSize() const { return vole->vSize(); }
	size_t vPadded() const { return vole->vPadded(); }

	u64 baseOtCount() const override final
	{
		// Can only use base OTs in groups of fieldBits.
		return roundUpTo(gOtExtBaseOtCount, fieldBits());
	}

	bool hasBaseOts() const override final
	{
		return vole.has_value();
	}

	DotSemiHonestReceiver splitBase()
	{
		throw RTE_LOC; // TODO: unimplemented.
	}

	std::unique_ptr<OtExtReceiver> split() override
	{
		return std::make_unique<DotSemiHonestReceiver>(splitBase());
	}

	void setBaseOts(
		span<std::array<block, 2>> baseSendOts,
		PRNG& prng, Channel& chl) override;

	virtual void initTemporaryStorage() { ChunkerBase::initTemporaryStorage(); }

	void receive(const BitVector& choices, span<block> messages, PRNG& prng, Channel& chl) override;

	// Low level functions.

	// Perform 128 random VOLEs (saving the messages up to send to the sender), and output the
	// choice bits (packed into a 128 bit block) and the chosen messages. The output is not
	// bitsliced, i.e. it is transposed from what the SubspaceVole outputs. outV must have length
	// vPadded() (which may be greater than 128). The extra blocks are treated as padding and may be
	// overwritten.
	void generateRandom(size_t blockIdx, block& randomU, span<block> outV)
	{
		vole->generateRandom(blockIdx, span<block>(&randomU, 1), outV);
		transpose128(outV.data());
	}

	void generateChosen(size_t blockIdx, block chosenU, span<block> outV)
	{
		vole->generateChosen(blockIdx, span<block>(&chosenU, 1), outV);
		transpose128(outV.data());
	}

protected:
	using ChunkerBase = ChunkedSender<DotSemiHonestReceiver, std::tuple<block>>;
	friend ChunkerBase;
	friend ChunkerBase::Base;

	static const size_t chunkSize = 128;
	static const size_t commSize = commStepSize * superBlkSize; // picked to match the other OTs.
	size_t paddingSize() const { return vPadded() - chunkSize; }

	void reserveSendBuffer(size_t batchSize) { vole->reserveMessages(0, batchSize); }
	void sendBuffer(Channel& chl) { vole->send(chl); }
	TRY_FORCEINLINE void processChunk(size_t numUsed, span<block> messages, block chioces);
};

}
}
#endif
