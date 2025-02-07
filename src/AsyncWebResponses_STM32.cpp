/****************************************************************************************************************************
  AsyncWebResponses_STM32.cpp - Dead simple AsyncWebServer for STM32 LAN8720 or built-in LAN8742A Ethernet

  For STM32 with LAN8720 (STM32F4/F7) or built-in LAN8742A Ethernet (Nucleo-144, DISCOVERY, etc)

  AsyncWebServer_STM32 is a library for the STM32 with LAN8720 or built-in LAN8742A Ethernet WebServer

  Based on and modified from ESPAsyncWebServer (https://github.com/me-no-dev/ESPAsyncWebServer)
  Built by Khoi Hoang https://github.com/khoih-prog/AsyncWebServer_STM32
  Licensed under MIT license

  Version: 1.3.0

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.2.3   K Hoang      02/09/2020 Initial coding for STM32 for built-in Ethernet (Nucleo-144, DISCOVERY, etc).
                                  Bump up version to v1.2.3 to sync with ESPAsyncWebServer v1.2.3
  1.2.4   K Hoang      05/09/2020 Add back MD5/SHA1 authentication feature.
  1.2.5   K Hoang      28/12/2020 Suppress all possible compiler warnings. Add examples.
  1.2.6   K Hoang      22/03/2021 Fix dependency on STM32AsyncTCP Library
  1.3.0   K Hoang      14/04/2021 Add support to LAN8720 using STM32F4 or STM32F7
 *****************************************************************************************************************************/

#define _ASYNCWEBSERVER_STM32_LOGLEVEL_     1

#include "AsyncWebServer_Debug_STM32.h"

#include "AsyncWebServer_STM32.h"
#include "AsyncWebResponseImpl_STM32.h"
#include "cbuf.h"

// Since ESP8266 does not link memchr by default, here's its implementation.
void* memchr(void* ptr, int ch, size_t count)
{
  unsigned char* p = static_cast<unsigned char*>(ptr);

  while (count--)
  {
    if (*p++ == static_cast<unsigned char>(ch))
      return --p;
  }

  return nullptr;
}

/*
   Abstract Response
 * */
const char* AsyncWebServerResponse::_responseCodeToString(int code)
{
  switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Time-out";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Request Entity Too Large";
    case 414: return "Request-URI Too Large";
    case 415: return "Unsupported Media Type";
    case 416: return "Requested range not satisfiable";
    case 417: return "Expectation Failed";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Time-out";
    case 505: return "HTTP Version not supported";
    default:  return "";
  }
}

AsyncWebServerResponse::AsyncWebServerResponse()
  : _code(0)
  , _headers(LinkedList<AsyncWebHeader * >([](AsyncWebHeader * h)
{
  delete h;
}))
, _contentType(), _contentLength(0), _sendContentLength(true), _chunked(false), _headLength(0)
, _sentLength(0), _ackedLength(0), _writtenLength(0), _state(RESPONSE_SETUP)
{
  for (auto header : DefaultHeaders::Instance())
  {
    _headers.add(new AsyncWebHeader(header->name(), header->value()));
  }
}

AsyncWebServerResponse::~AsyncWebServerResponse()
{
  _headers.free();
}

void AsyncWebServerResponse::setCode(int code)
{
  if (_state == RESPONSE_SETUP)
    _code = code;
}

void AsyncWebServerResponse::setContentLength(size_t len)
{
  if (_state == RESPONSE_SETUP)
    _contentLength = len;
}

void AsyncWebServerResponse::setContentType(const String& type)
{
  if (_state == RESPONSE_SETUP)
    _contentType = type;
}

void AsyncWebServerResponse::addHeader(const String& name, const String& value)
{
  _headers.add(new AsyncWebHeader(name, value));
}

String AsyncWebServerResponse::_assembleHead(uint8_t version)
{
  if (version)
  {
    addHeader("Accept-Ranges", "none");

    if (_chunked)
      addHeader("Transfer-Encoding", "chunked");
  }

  String out = String();
  int bufSize = 300;
  char buf[bufSize];

  snprintf(buf, bufSize, "HTTP/1.%d %d %s\r\n", version, _code, _responseCodeToString(_code));
  out.concat(buf);

  if (_sendContentLength)
  {
    snprintf(buf, bufSize, "Content-Length: %d\r\n", _contentLength);
    out.concat(buf);
  }

  if (_contentType.length())
  {
    snprintf(buf, bufSize, "Content-Type: %s\r\n", _contentType.c_str());
    out.concat(buf);
  }

  for (const auto& header : _headers)
  {
    snprintf(buf, bufSize, "%s: %s\r\n", header->name().c_str(), header->value().c_str());
    out.concat(buf);
  }

  _headers.free();

  out.concat("\r\n");
  _headLength = out.length();

  return out;
}

bool AsyncWebServerResponse::_started() const
{
  return _state > RESPONSE_SETUP;
}

bool AsyncWebServerResponse::_finished() const
{
  return _state > RESPONSE_WAIT_ACK;
}

bool AsyncWebServerResponse::_failed() const
{
  return _state == RESPONSE_FAILED;
}

bool AsyncWebServerResponse::_sourceValid() const
{
  return false;
}

void AsyncWebServerResponse::_respond(AsyncWebServerRequest *request)
{
  _state = RESPONSE_END;
  request->client()->close();
}

size_t AsyncWebServerResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time)
{
  (void)request;
  (void)len;
  (void)time;
  return 0;
}

/*
   String/Code Response
 * */
AsyncBasicResponse::AsyncBasicResponse(int code, const String& contentType, const String& content)
{
  _code = code;
  _content = content;
  _contentType = contentType;

  if (_content.length())
  {
    _contentLength = _content.length();
    if (!_contentType.length())
      _contentType = "text/plain";
  }

  addHeader("Connection", "close");
}

void AsyncBasicResponse::_respond(AsyncWebServerRequest *request)
{
  _state = RESPONSE_HEADERS;
  String out = _assembleHead(request->version());
  size_t outLen = out.length();
  size_t space = request->client()->space();

  if (!_contentLength && space >= outLen)
  {
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  }
  else if (_contentLength && space >= outLen + _contentLength)
  {
    out += _content;
    outLen += _contentLength;
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  }
  else if (space && space < outLen)
  {
    String partial = out.substring(0, space);
    _content = out.substring(space) + _content;
    _contentLength += outLen - space;
    _writtenLength += request->client()->write(partial.c_str(), partial.length());
    _state = RESPONSE_CONTENT;
  }
  else if (space > outLen && space < (outLen + _contentLength))
  {
    size_t shift = space - outLen;
    outLen += shift;
    _sentLength += shift;
    out += _content.substring(0, shift);
    _content = _content.substring(shift);
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_CONTENT;
  }
  else
  {
    _content = out + _content;
    _contentLength += outLen;
    _state = RESPONSE_CONTENT;
  }
}

size_t AsyncBasicResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time)
{
  (void)time;
  _ackedLength += len;

  if (_state == RESPONSE_CONTENT)
  {
    size_t available = _contentLength - _sentLength;
    size_t space = request->client()->space();

    //we can fit in this packet
    if (space > available)
    {
      _writtenLength += request->client()->write(_content.c_str(), available);
      _content = String();
      _state = RESPONSE_WAIT_ACK;

      return available;
    }

    //send some data, the rest on ack
    String out = _content.substring(0, space);
    _content = _content.substring(space);
    _sentLength += space;
    _writtenLength += request->client()->write(out.c_str(), space);

    return space;
  }
  else if (_state == RESPONSE_WAIT_ACK)
  {
    if (_ackedLength >= _writtenLength)
    {
      _state = RESPONSE_END;
      request->client()->close(true);  /* Might it be required? */
    }
  }

  return 0;
}


/*
   Abstract Response
 * */

AsyncAbstractResponse::AsyncAbstractResponse(AwsTemplateProcessor callback): _callback(callback)
{
  // In case of template processing, we're unable to determine real response size
  if (callback)
  {
    _contentLength = 0;
    _sendContentLength = false;
    _chunked = true;
  }
}

void AsyncAbstractResponse::_respond(AsyncWebServerRequest *request)
{
  addHeader("Connection", "close");
  _head = _assembleHead(request->version());
  _state = RESPONSE_HEADERS;
  _ack(request, 0, 0);
}

size_t AsyncAbstractResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time)
{
  (void)time;

  if (!_sourceValid())
  {
    _state = RESPONSE_FAILED;
    request->client()->close();

    return 0;
  }

  _ackedLength += len;
  size_t space = request->client()->space();

  size_t headLen = _head.length();

  if (_state == RESPONSE_HEADERS)
  {
    if (space >= headLen)
    {
      _state = RESPONSE_CONTENT;
      space -= headLen;
    }
    else
    {
      String out = _head.substring(0, space);
      _head = _head.substring(space);
      _writtenLength += request->client()->write(out.c_str(), out.length());

      return out.length();
    }
  }

  if (_state == RESPONSE_CONTENT)
  {
    size_t outLen;

    if (_chunked)
    {
      if (space <= 8)
      {
        return 0;
      }

      outLen = space;
    }
    else if (!_sendContentLength)
    {
      outLen = space;
    }
    else
    {
      outLen = ((_contentLength - _sentLength) > space) ? space : (_contentLength - _sentLength);
    }

    uint8_t *buf = (uint8_t *)malloc(outLen + headLen);

    if (!buf)
    {
      LOGDEBUG1("AsyncAbstractResponse::_ack malloc failed, size =", outLen + headLen);

      return 0;
    }

    if (headLen)
    {
      memcpy(buf, _head.c_str(), _head.length());
    }

    size_t readLen = 0;

    if (_chunked)
    {
      // HTTP 1.1 allows leading zeros in chunk length. Or spaces may be added.
      // See RFC2616 sections 2, 3.6.1.
      readLen = _fillBufferAndProcessTemplates(buf + headLen + 6, outLen - 8);

      if (readLen == RESPONSE_TRY_AGAIN)
      {
        free(buf);
        return 0;
      }

      outLen = sprintf((char*)buf + headLen, "%x", readLen) + headLen;

      while (outLen < headLen + 4)
        buf[outLen++] = ' ';

      buf[outLen++] = '\r';
      buf[outLen++] = '\n';
      outLen += readLen;
      buf[outLen++] = '\r';
      buf[outLen++] = '\n';
    }
    else
    {
      readLen = _fillBufferAndProcessTemplates(buf + headLen, outLen);

      if (readLen == RESPONSE_TRY_AGAIN)
      {
        free(buf);
        return 0;
      }

      outLen = readLen + headLen;
    }

    if (headLen)
    {
      _head = String();
    }

    if (outLen)
    {
      _writtenLength += request->client()->write((const char*)buf, outLen);
    }

    if (_chunked) {
      _sentLength += readLen;
    }
    else
    {
      _sentLength += outLen - headLen;
    }

    free(buf);

    if ((_chunked && readLen == 0) || (!_sendContentLength && outLen == 0) || (!_chunked && _sentLength == _contentLength))
    {
      _state = RESPONSE_WAIT_ACK;
    }

    return outLen;

  }
  else if (_state == RESPONSE_WAIT_ACK)
  {
    if (!_sendContentLength || _ackedLength >= _writtenLength)
    {
      _state = RESPONSE_END;

      if (!_chunked && !_sendContentLength)
        request->client()->close(true);
    }
  }

  return 0;
}

size_t AsyncAbstractResponse::_readDataFromCacheOrContent(uint8_t* data, const size_t len)
{
  // If we have something in cache, copy it to buffer
  const size_t readFromCache = std::min(len, _cache.size());

  if (readFromCache)
  {
    memcpy(data, _cache.data(), readFromCache);
    _cache.erase(_cache.begin(), _cache.begin() + readFromCache);
  }

  // If we need to read more...
  const size_t needFromFile = len - readFromCache;
  const size_t readFromContent = _fillBuffer(data + readFromCache, needFromFile);

  return readFromCache + readFromContent;
}

size_t AsyncAbstractResponse::_fillBufferAndProcessTemplates(uint8_t* data, size_t len)
{
  if (!_callback)
    return _fillBuffer(data, len);

  const size_t originalLen = len;
  len = _readDataFromCacheOrContent(data, len);

  // Now we've read 'len' bytes, either from cache or from file
  // Search for template placeholders
  uint8_t* pTemplateStart = data;

  while ((pTemplateStart < &data[len]) && (pTemplateStart = (uint8_t*) memchr(pTemplateStart, TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart + 1)))
  {
    // data[0] ... data[len - 1]
    uint8_t* pTemplateEnd = (pTemplateStart < &data[len - 1]) ? (uint8_t*) memchr(pTemplateStart + 1, TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart) : nullptr;

    // temporary buffer to hold parameter name
    uint8_t buf[TEMPLATE_PARAM_NAME_LENGTH + 1];
    String paramName;

    // If closing placeholder is found:
    if (pTemplateEnd)
    {
      // prepare argument to callback
      const size_t paramNameLength = std::min(sizeof(buf) - 1, (unsigned int)(pTemplateEnd - pTemplateStart - 1));

      if (paramNameLength)
      {
        memcpy(buf, pTemplateStart + 1, paramNameLength);
        buf[paramNameLength] = 0;
        paramName = String(reinterpret_cast<char*>(buf));
      }
      else
      {
        // double percent sign encountered, this is single percent sign escaped.
        // remove the 2nd percent sign
        memmove(pTemplateEnd, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
        len += _readDataFromCacheOrContent(&data[len - 1], 1) - 1;
        ++pTemplateStart;
      }
    }
    else if (&data[len - 1] - pTemplateStart + 1 < TEMPLATE_PARAM_NAME_LENGTH + 2)
    {
      // closing placeholder not found, check if it's in the remaining file data
      memcpy(buf, pTemplateStart + 1, &data[len - 1] - pTemplateStart);
      const size_t readFromCacheOrContent = _readDataFromCacheOrContent(buf + (&data[len - 1] - pTemplateStart), TEMPLATE_PARAM_NAME_LENGTH + 2 - (&data[len - 1] - pTemplateStart + 1));

      if (readFromCacheOrContent)
      {
        pTemplateEnd = (uint8_t*)memchr(buf + (&data[len - 1] - pTemplateStart), TEMPLATE_PLACEHOLDER, readFromCacheOrContent);

        if (pTemplateEnd)
        {
          // prepare argument to callback
          *pTemplateEnd = 0;
          paramName = String(reinterpret_cast<char*>(buf));
          // Copy remaining read-ahead data into cache
          _cache.insert(_cache.begin(), pTemplateEnd + 1, buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
          pTemplateEnd = &data[len - 1];
        }
        else // closing placeholder not found in file data, store found percent symbol as is and advance to the next position
        {
          // but first, store read file data in cache
          _cache.insert(_cache.begin(), buf + (&data[len - 1] - pTemplateStart), buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
          ++pTemplateStart;
        }
      }
      else // closing placeholder not found in content data, store found percent symbol as is and advance to the next position
        ++pTemplateStart;
    }
    else // closing placeholder not found in content data, store found percent symbol as is and advance to the next position
      ++pTemplateStart;

    if (paramName.length())
    {
      // call callback and replace with result.
      // Everything in range [pTemplateStart, pTemplateEnd] can be safely replaced with parameter value.
      // Data after pTemplateEnd may need to be moved.
      // The first byte of data after placeholder is located at pTemplateEnd + 1.
      // It should be located at pTemplateStart + numBytesCopied (to begin right after inserted parameter value).
      const String paramValue(_callback(paramName));
      const char* pvstr = paramValue.c_str();
      const unsigned int pvlen = paramValue.length();
      const size_t numBytesCopied = std::min(pvlen, static_cast<unsigned int>(&data[originalLen - 1] - pTemplateStart + 1));
      // make room for param value

      // 1. move extra data to cache if parameter value is longer than placeholder AND if there is no room to store
      if ((pTemplateEnd + 1 < pTemplateStart + numBytesCopied) && (originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1) < len))
      {
        _cache.insert(_cache.begin(), &data[originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1)], &data[len]);

        //2. parameter value is longer than placeholder text, push the data after placeholder which not saved into cache further to the end
        memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[originalLen] - pTemplateStart - numBytesCopied);
        len = originalLen; // fix issue with truncated data, not sure if it has any side effects
      }
      else if (pTemplateEnd + 1 != pTemplateStart + numBytesCopied)
      {
        //2. Either parameter value is shorter than placeholder text OR there is enough free space in buffer to fit.
        //   Move the entire data after the placeholder
        memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
      }

      // 3. replace placeholder with actual value
      memcpy(pTemplateStart, pvstr, numBytesCopied);

      // If result is longer than buffer, copy the remainder into cache (this could happen only if placeholder text itself did not fit entirely in buffer)
      if (numBytesCopied < pvlen)
      {
        _cache.insert(_cache.begin(), pvstr + numBytesCopied, pvstr + pvlen);
      }
      else if (pTemplateStart + numBytesCopied < pTemplateEnd + 1)
      {
        // result is copied fully; if result is shorter than placeholder text...
        // there is some free room, fill it from cache
        const size_t roomFreed = pTemplateEnd + 1 - pTemplateStart - numBytesCopied;
        const size_t totalFreeRoom = originalLen - len + roomFreed;
        len += _readDataFromCacheOrContent(&data[len - roomFreed], totalFreeRoom) - roomFreed;
      }
      else
      {
        // result is copied fully; it is longer than placeholder text
        const size_t roomTaken = pTemplateStart + numBytesCopied - pTemplateEnd - 1;
        len = std::min(len + roomTaken, originalLen);
      }
    }
  } // while(pTemplateStart)

  return len;
}

/*
   Stream Response
 * */

AsyncStreamResponse::AsyncStreamResponse(Stream &stream, const String& contentType, size_t len, AwsTemplateProcessor callback): AsyncAbstractResponse(callback)
{
  _code = 200;
  _content = &stream;
  _contentLength = len;
  _contentType = contentType;
}

size_t AsyncStreamResponse::_fillBuffer(uint8_t *data, size_t len)
{
  size_t available = _content->available();
  size_t outLen = (available > len) ? len : available;
  size_t i;

  for (i = 0; i < outLen; i++)
    data[i] = _content->read();

  return outLen;
}

/*
   Callback Response
 * */

AsyncCallbackResponse::AsyncCallbackResponse(const String& contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback)
  : AsyncAbstractResponse(templateCallback)
{
  _code = 200;
  _content = callback;
  _contentLength = len;

  if (!len)
    _sendContentLength = false;

  _contentType = contentType;
  _filledLength = 0;
}

size_t AsyncCallbackResponse::_fillBuffer(uint8_t *data, size_t len)
{
  size_t ret = _content(data, len, _filledLength);

  if (ret != RESPONSE_TRY_AGAIN)
  {
    _filledLength += ret;
  }

  return ret;
}

/*
   Chunked Response
 * */

AsyncChunkedResponse::AsyncChunkedResponse(const String& contentType, AwsResponseFiller callback, AwsTemplateProcessor processorCallback): AsyncAbstractResponse(processorCallback)
{
  _code = 200;
  _content = callback;
  _contentLength = 0;
  _contentType = contentType;
  _sendContentLength = false;
  _chunked = true;
  _filledLength = 0;
}

size_t AsyncChunkedResponse::_fillBuffer(uint8_t *data, size_t len)
{
  size_t ret = _content(data, len, _filledLength);

  if (ret != RESPONSE_TRY_AGAIN)
  {
    _filledLength += ret;
  }

  return ret;
}

/*
 * Progmem Response
 * */
AsyncProgmemResponse::AsyncProgmemResponse(int code, const String& contentType, const uint8_t * content, size_t len, AwsTemplateProcessor callback): AsyncAbstractResponse(callback) {
  _code = code;
  _content = content;
  _contentType = contentType;
  _contentLength = len;
  _readLength = 0;
}

size_t AsyncProgmemResponse::_fillBuffer(uint8_t *data, size_t len){
  size_t left = _contentLength - _readLength;
  if (left > len) {
    memcpy_P(data, _content + _readLength, len);
    _readLength += len;
    return len;
  }
  memcpy_P(data, _content + _readLength, left);
  _readLength += left;
  return left;
}

/*
   Response Stream (You can print/write/printf to it, up to the contentLen bytes)
 * */

AsyncResponseStream::AsyncResponseStream(const String& contentType, size_t bufferSize)
{
  _code = 200;
  _contentLength = 0;
  _contentType = contentType;
  _content = new cbuf(bufferSize);
}

AsyncResponseStream::~AsyncResponseStream()
{
  delete _content;
}

size_t AsyncResponseStream::_fillBuffer(uint8_t *buf, size_t maxLen)
{
  return _content->read((char*)buf, maxLen);
}

size_t AsyncResponseStream::write(const uint8_t *data, size_t len)
{
  if (_started())
    return 0;

  if (len > _content->room())
  {
    size_t needed = len - _content->room();
    _content->resizeAdd(needed);
  }

  size_t written = _content->write((const char*)data, len);
  _contentLength += written;

  return written;
}

size_t AsyncResponseStream::write(uint8_t data)
{
  return write(&data, 1);
}
