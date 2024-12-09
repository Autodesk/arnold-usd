#include <ai.h>

#include <cstdlib>
int main(int argc, char **argv)
{
	if (std::getenv("PROCEDURAL_USE_HYDRA")!=nullptr) {
		return 1;
	}
	return 0;
}
