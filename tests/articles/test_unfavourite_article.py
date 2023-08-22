import pytest
import time
from http import HTTPStatus

from endpoints import register_user, create_article, get_article, favourite_article, unfavourite_article
from models import User, Profile, Article
from validators import validate_article
from utils import get_user_token


async def test_unfavourite_article_unauthorized(service_client):
    user = User(bio=None, image=None)
    response = await register_user(service_client, user)
    assert response.status == HTTPStatus.OK

    article = Article(Profile(user))
    response = await create_article(service_client, article, get_user_token(response))
    assert response.status == HTTPStatus.OK

    response = await unfavourite_article(service_client, article, None)
    assert response.status == HTTPStatus.UNAUTHORIZED


async def test_unfavourite_unknown_article(service_client):
    user = User(bio=None, image=None)
    response = await register_user(service_client, user)
    assert response.status == HTTPStatus.OK

    article = Article(Profile(user))

    response = await unfavourite_article(service_client, article, get_user_token(response))
    assert response.status == HTTPStatus.NOT_FOUND


async def test_unfavourite_article(service_client):
    user = User(bio=None, image=None)
    response = await register_user(service_client, user)
    assert response.status == HTTPStatus.OK

    user_token = get_user_token(response)

    article = Article(Profile(user))
    response = await create_article(service_client, article, user_token)
    assert response.status == HTTPStatus.OK

    another_user = User(bio=None, image=None)
    response = await register_user(service_client, another_user)
    assert response.status == HTTPStatus.OK

    another_user_token = get_user_token(response)

    response = await favourite_article(service_client, article, another_user_token)
    assert response.status == HTTPStatus.OK

    response = await unfavourite_article(service_client, article, another_user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)

    time.sleep(10)
    response = await get_article(service_client, article, another_user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)

    response = await unfavourite_article(service_client, article, another_user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)

    time.sleep(10)
    response = await get_article(service_client, article, another_user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)

    response = await unfavourite_article(service_client, article, user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)

    time.sleep(10)
    response = await get_article(service_client, article, user_token)
    assert response.status == HTTPStatus.OK
    assert validate_article(article, response)
