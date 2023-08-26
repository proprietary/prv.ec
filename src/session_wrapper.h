#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_WEB_SESSION_WRAPER_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_WEB_SESSION_WRAPER_H

#include <proxygen/lib/http/session/HTTPUpstreamSession.h>

namespace ec_prv {
  namespace url_shortener {
    namespace web {
      class SessionWrapper : public proxygen::HTTPSession::InfoCallback {
      private:
	proxygen::HTTPUpstreamSession *session_{nullptr};
      public:
	explicit SessionWrapper(proxygen::HTTPUpstreamSession* session) : session_(session) {
	  session_->setInfoCallback(this);
	}

	~SessionWrapper() override {
	  if (session_ != nullptr) {
	    session_->drain();
	  }
	}

	proxygen::HTTPUpstreamSession* operator-() const {
	  return session_;
	}

	void onDestroy(const proxygen::HTTPSessionBase&) override {
	  session_ = nullptr;
	}
      };
    } // namespace web
  } // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER_WEB_SESSION_WRAPER_H
