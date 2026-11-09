#include <criterion/criterion.h>
#include <stdlib.h>

#include "config.h"

static void setup(void)
{
    setenv("DNS_PORT", "5300", 1);
    setenv("UPSTREAM_DNS", "8.8.8.8", 1);
    setenv("UPSTREAM_DNS_PORT", "53", 1);
    setenv("UPSTREAM_TIMEOUT_SECONDS", "3", 1);
    setenv("DNS_TTL_SECONDS", "0", 1);
    setenv("NODE_API_URL", "https://api.example.test", 1);
    setenv("NODE_API_KEY", "test-key", 1);
    setenv("NODE_API_TIMEOUT_SECONDS", "5", 1);
}

TestSuite(config, .init = setup);

Test(config, accepts_valid_values_and_zero_ttl)
{
    Config config;
    cr_assert_eq(config_from_env(&config), 0);
    cr_assert_eq(config.listen_port, 5300);
    cr_assert_eq(ntohs(config.upstream_address.sin_port), 53);
    cr_assert_eq(config.dns_ttl_seconds, 0);
    cr_assert_str_eq(config.node_api_key, "test-key");
}

Test(config, rejects_invalid_numbers)
{
    const char *ports[] = {"0", "65536", "-1", " 53", "53x", ""};
    Config config;
    for(size_t i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        setenv("DNS_PORT", ports[i], 1);
        cr_assert_neq(config_from_env(&config), 0);
    }
    setenv("DNS_PORT", "5300", 1);
    setenv("DNS_TTL_SECONDS", "4294967296", 1);
    cr_assert_neq(config_from_env(&config), 0);
    setenv("DNS_TTL_SECONDS", "0", 1);
    setenv("UPSTREAM_TIMEOUT_SECONDS", "0", 1);
    cr_assert_neq(config_from_env(&config), 0);
}

Test(config, rejects_invalid_address_key_and_missing_variables)
{
    Config config;
    setenv("UPSTREAM_DNS", "not-an-ip", 1);
    cr_assert_neq(config_from_env(&config), 0);
    setenv("UPSTREAM_DNS", "8.8.8.8", 1);
    unsetenv("NODE_API_URL");
    cr_assert_neq(config_from_env(&config), 0);
    setenv("NODE_API_URL", "http://127.0.0.1:8787", 1);
    cr_assert_eq(config_from_env(&config), 0);
    setenv("NODE_API_KEY", "key\r\nInjected: header", 1);
    cr_assert_neq(config_from_env(&config), 0);
    unsetenv("NODE_API_KEY");
    cr_assert_neq(config_from_env(&config), 0);
}
