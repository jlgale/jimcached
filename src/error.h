#include <iostream>

class base_error
{
};

class error : public base_error
{
};

class client_error : public base_error
{
};

class server_error : public base_error
{
};
