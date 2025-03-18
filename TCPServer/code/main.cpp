#include "server/server.h"

int main(){
    Server server(9000, 8, true, 0,
                  0, "mmapTest",
                  "test", 10000
    );
    server.start();
    return 0;
}