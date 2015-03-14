#ifndef CLIENT_H
#define CLIENT_H

#include <rct/Connection.h>

class Client
{
public:
    Client();
    ~Client();

    bool run(int argc, char** argv);
    int exitCode() const { return mConnection->finishStatus(); }

private:
    std::shared_ptr<Connection> mConnection;
};

#endif
