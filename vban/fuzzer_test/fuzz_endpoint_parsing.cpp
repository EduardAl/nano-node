#include <vban/node/common.hpp>

/** Fuzz endpoint parsing */
void fuzz_endpoint_parsing (const uint8_t * Data, size_t Size)
{
	auto data (std::string (reinterpret_cast<char *> (const_cast<uint8_t *> (Data)), Size));
	vban::endpoint endpoint;
	vban::parse_endpoint (data, endpoint);
	vban::tcp_endpoint tcp_endpoint;
	vban::parse_tcp_endpoint (data, tcp_endpoint);
}

/** Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput (const uint8_t * Data, size_t Size)
{
	fuzz_endpoint_parsing (Data, Size);
	return 0;
}
