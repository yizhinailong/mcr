/**
 * @file mcr.cppm
 * @brief Library entry module re-exporting the public request-mcpp interfaces.
 */
export module mcr;

export import mcr.api;
export import mcr.async;
export import mcr.async_wrapper;
export import mcr.auth;
export import mcr.body;
export import mcr.buffer;
export import mcr.callback;
export import mcr.cert_info;
export import mcr.connection_pool;
export import mcr.cookies;
export import mcr.curl_container;
export import mcr.curlholder;
export import mcr.curlmultiholder;
export import mcr.error;
export import mcr.fields;
export import mcr.file;
export import mcr.http;
export import mcr.interface;
export import mcr.multipart;
export import mcr.proxy;
export import mcr.response;
export import mcr.secure_string;
export import mcr.session;
export import mcr.singleton;
export import mcr.sse;
export import mcr.ssl_ctx;
export import mcr.ssl_options;
export import mcr.status_code;
export import mcr.threadpool;
export import mcr.transfer_options;
export import mcr.types;
export import mcr.util;
export import mcr.version;
